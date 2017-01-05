#define XBYAK_NO_OP_NAMES
#include <xbyak/xbyak.h>

extern "C" {
#include "hw/sh4/sh4.h"
#include "hw/sh4/x64/sh4_dispatch.h"
#include "jit/jit.h"
}

/* executable sh4 code sits between 0x0c000000 and 0x0d000000. each instr is 2
   bytes, making for a maximum of 0x800000 blocks */
#define CODE_SIZE 0x800000
#define CACHE_SIZE 0x800000
#define DISPATCH_SIZE 1024

/* place code buffer in data segment (as opposed to allocating on the heap) to
   keep it within 2 GB of the code segment, enabling the x64 backend to use
   RIP-relative offsets when calling functions */
uint8_t sh4_code[CODE_SIZE];
int sh4_code_size = CODE_SIZE;
int sh4_stack_size = 1024;
static void *sh4_cache[CACHE_SIZE];
static int sh4_cache_size = CACHE_SIZE;
static uint8_t sh4_dispatch[DISPATCH_SIZE];

/* controls if edges are added and managed between static branches. the first
   time each branch is hit, its destination block will be dynamically looked
   up. if this is enabled, an edge will be added between the two blocks, and
   the branch will be patched to directly jmp to the destination block,
   avoiding the need for redundant lookups */
#define LINK_STATIC_BRANCHES 1

void *sh4_dispatch_dynamic;
void *sh4_dispatch_static;
void *sh4_dispatch_compile;
void *sh4_dispatch_interrupt;
void (*sh4_dispatch_enter)();
void *sh4_dispatch_leave;

#if PLATFORM_WINDOWS
static const Xbyak::Reg64 arg0 = Xbyak::util::rcx;
static const Xbyak::Reg64 arg1 = Xbyak::util::rdx;
static const Xbyak::Reg64 arg2 = Xbyak::util::r8;
#else
static const Xbyak::Reg64 arg0 = Xbyak::util::rdi;
static const Xbyak::Reg64 arg1 = Xbyak::util::rsi;
static const Xbyak::Reg64 arg2 = Xbyak::util::rdx;
#endif

static Xbyak::CodeGenerator code_emitter(CODE_SIZE, sh4_code);
static Xbyak::CodeGenerator dispatch_emitter(DISPATCH_SIZE, sh4_dispatch);

static void **sh4_dispatch_code_ptr(uint32_t addr) {
  return &sh4_cache[(addr & 0x00ffffff) >> 1];
}

void sh4_dispatch_restore_edge(void *code, uint32_t dst) {
  auto &e = code_emitter;
  size_t original_size = e.getSize();
  e.setSize((uint8_t *)code - e.getCode());
  e.call(sh4_dispatch_static);
  e.setSize(original_size);
}

void sh4_dispatch_patch_edge(void *code, void *dst) {
  auto &e = code_emitter;
  size_t original_size = e.getSize();
  e.setSize((uint8_t *)code - e.getCode());
  e.jmp(dst);
  e.setSize(original_size);
}

void sh4_dispatch_invalidate_code(uint32_t addr) {
  void **entry = sh4_dispatch_code_ptr(addr);
  *entry = sh4_dispatch_compile;
}

void sh4_dispatch_cache_code(uint32_t addr, void *code) {
  void **entry = sh4_dispatch_code_ptr(addr);
  CHECK_EQ(*entry, sh4_dispatch_compile);
  *entry = code;
}

void *sh4_dispatch_lookup_code(uint32_t addr) {
  void **entry = sh4_dispatch_code_ptr(addr);
  return *entry;
}

static void sh4_dispatch_reset() {
  for (int i = 0; i < CACHE_SIZE; i++) {
    sh4_cache[i] = sh4_dispatch_compile;
  }
}

void sh4_dispatch_init(void *sh4, void *jit, void *ctx, void *mem) {
  /* ensure both codegen buffers are marked as executable */
  CHECK(Xbyak::CodeArray::protect(sh4_code, sizeof(sh4_code), true));
  CHECK(Xbyak::CodeArray::protect(sh4_dispatch, sizeof(sh4_dispatch), true));

  /* emit dispatch related thunks */
  auto &e = dispatch_emitter;

  e.reset();

  {
    /* called after a dynamic branch instruction stores the next pc to the
       context. looks up the host block for it jumps to it */
    e.align(32);

    sh4_dispatch_dynamic = e.getCurr<void *>();

    e.mov(e.rax, (uint64_t)sh4_cache);
    e.mov(e.ecx, e.dword[e.r14 + offsetof(struct sh4_ctx, pc)]);
    e.and_(e.ecx, 0x00ffffff);
    e.jmp(e.qword[e.rax + e.rcx * 4]);
  }

  {
    /* called after a static branch instruction stores the next pc to the
       context. the thunk calls jit_add_edge which adds an edge between the
       calling block and the branch destination block, and then falls through
       to the above dynamic branch thunk. on the second run through this code
       jit_add_edge will call sh4_dispatch_patch_edge, patching the caller to
       directly jump to the destination block */
    e.align(32);

    sh4_dispatch_static = e.getCurr<void *>();

#if LINK_STATIC_BRANCHES
    e.mov(arg0, (uint64_t)jit);
    e.pop(arg1);
    e.sub(arg1, 5 /* sizeof jmp instr */);
    e.mov(arg2, e.dword[e.r14 + offsetof(struct sh4_ctx, pc)]);
    e.call(&jit_add_edge);
#else
    e.pop(arg1);
#endif
    e.jmp(sh4_dispatch_dynamic);
  }

  {
    /* default cache entry for all blocks. compiles the desired pc before
       jumping to the block through the dynamic dispatch thunk */
    e.align(32);

    sh4_dispatch_compile = e.getCurr<void *>();

    e.mov(arg0, (uint64_t)jit);
    e.mov(arg1, e.dword[e.r14 + offsetof(struct sh4_ctx, pc)]);
    e.call(&jit_compile_block);
    e.jmp(sh4_dispatch_dynamic);
  }

  {
    /* processes the pending interrupt request, and then jumps to the new pc
       through the dynamic dispatch thunk */
    e.align(32);

    sh4_dispatch_interrupt = e.getCurr<void *>();

    e.mov(arg0, (uint64_t)sh4);
    e.call(&sh4_intc_check_pending);
    e.jmp(sh4_dispatch_dynamic);
  }

  {
    /* entry point to the compiled sh4 code. sets up the stack frame, sets up
       fixed registers (context and memory base) and then jumps to the current
       pc through the dynamic dispatch thunk */
    e.align(32);

    sh4_dispatch_enter = e.getCurr<void (*)()>();

    e.push(e.rbx);
    e.push(e.rbp);
#if PLATFORM_WINDOWS
    e.push(e.rdi);
    e.push(e.rsi);
#endif
    e.push(e.r12);
    e.push(e.r13);
    e.push(e.r14);
    e.push(e.r15);
    e.sub(e.rsp, sh4_stack_size + 8);
    e.mov(e.r14, (uint64_t)ctx);
    e.mov(e.r15, (uint64_t)mem);
    e.jmp(sh4_dispatch_dynamic);
  }

  {
    /* exit point for the compiled sh4 code, tears down the stack frame and
       returns */
    e.align(32);

    sh4_dispatch_leave = e.getCurr<void *>();

    e.add(e.rsp, sh4_stack_size + 8);
    e.pop(e.r15);
    e.pop(e.r14);
    e.pop(e.r13);
    e.pop(e.r12);
#if PLATFORM_WINDOWS
    e.pop(e.rsi);
    e.pop(e.rdi);
#endif
    e.pop(e.rbp);
    e.pop(e.rbx);
    e.ret();
  }

  /* reset cache after compiling the thunks it references */
  sh4_dispatch_reset();
}
