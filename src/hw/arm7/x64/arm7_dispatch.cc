#define XBYAK_NO_OP_NAMES
#include <xbyak/xbyak.h>

extern "C" {
#include "hw/arm7/arm7.h"
#include "hw/arm7/x64/arm7_dispatch.h"
#include "jit/frontend/armv3/armv3_context.h"
#include "jit/jit.h"
}

/* executable arm7 code sits between 0x0c000000 and 0x0d000000. each instr is 2
   bytes, making for a maximum of 0x800000 blocks */
#define CODE_SIZE 0x800000
#define CACHE_SIZE 0x200000
#define DISPATCH_SIZE 1024

/* place code buffer in data segment (as opposed to allocating on the heap) to
   keep it within 2 GB of the code segment, enabling the x64 backend to use
   RIP-relative offsets when calling functions */
uint8_t arm7_code[CODE_SIZE];
int arm7_code_size = CODE_SIZE;
int arm7_stack_size = 1024;
static void *arm7_cache[CACHE_SIZE];
static int arm7_cache_size = CACHE_SIZE;
static uint8_t arm7_dispatch[DISPATCH_SIZE];

void *arm7_dispatch_dynamic;
void *arm7_dispatch_static;
void *arm7_dispatch_compile;
void *arm7_dispatch_interrupt;
void (*arm7_dispatch_enter)();
void *arm7_dispatch_leave;

#if PLATFORM_WINDOWS
static const Xbyak::Reg64 arg0 = Xbyak::util::rcx;
static const Xbyak::Reg64 arg1 = Xbyak::util::rdx;
static const Xbyak::Reg64 arg2 = Xbyak::util::r8;
#else
static const Xbyak::Reg64 arg0 = Xbyak::util::rdi;
static const Xbyak::Reg64 arg1 = Xbyak::util::rsi;
static const Xbyak::Reg64 arg2 = Xbyak::util::rdx;
#endif

static Xbyak::CodeGenerator code_emitter(CODE_SIZE, arm7_code);
static Xbyak::CodeGenerator dispatch_emitter(DISPATCH_SIZE, arm7_dispatch);

static inline void **arm7_dispatch_code_ptr(uint32_t addr) {
  return &arm7_cache[(addr & 0x007fffff) >> 1];
}

void arm7_dispatch_restore_edge(void *code, uint32_t dst) {
  auto &e = code_emitter;
  size_t original_size = e.getSize();
  e.setSize((uint8_t *)code - e.getCode());
  e.call(arm7_dispatch_static);
  e.setSize(original_size);
}

void arm7_dispatch_patch_edge(void *code, void *dst) {
  auto &e = code_emitter;
  size_t original_size = e.getSize();
  e.setSize((uint8_t *)code - e.getCode());
  e.jmp(dst);
  e.setSize(original_size);
}

void arm7_dispatch_invalidate_code(uint32_t addr) {
  void **entry = arm7_dispatch_code_ptr(addr);
  *entry = arm7_dispatch_compile;
}

void arm7_dispatch_cache_code(uint32_t addr, void *code) {
  void **entry = arm7_dispatch_code_ptr(addr);
  CHECK_EQ(*entry, arm7_dispatch_compile);
  *entry = code;
}

void *arm7_dispatch_lookup_code(uint32_t addr) {
  void **entry = arm7_dispatch_code_ptr(addr);
  return *entry;
}

static void arm7_dispatch_reset() {
  for (int i = 0; i < CACHE_SIZE; i++) {
    arm7_cache[i] = arm7_dispatch_compile;
  }
}

void arm7_dispatch_init(void *sh4, void *jit, void *ctx, void *mem) {
  /* ensure both codegen buffers are marked as executable */
  CHECK(Xbyak::CodeArray::protect(arm7_code, sizeof(arm7_code), true));
  CHECK(Xbyak::CodeArray::protect(arm7_dispatch, sizeof(arm7_dispatch), true));

  /* emit dispatch related thunks */
  auto &e = dispatch_emitter;

  e.reset();

  {
    e.align(32);

    arm7_dispatch_dynamic = e.getCurr<void *>();

    e.mov(e.rax, (uint64_t)arm7_cache);
    e.mov(e.ecx, e.dword[e.r14 + offsetof(struct armv3_context, r[15])]);
    e.and_(e.ecx, 0x007fffff);
    e.jmp(e.qword[e.rax + e.rcx * 4]);
  }

  {
    e.align(32);

    arm7_dispatch_static = e.getCurr<void *>();

    e.mov(arg0, (uint64_t)jit);
    e.pop(arg1);
    e.sub(arg1, 5); /* sizeof jmp instr */
    e.mov(arg2, e.dword[e.r14 + offsetof(struct armv3_context, r[15])]);
    e.call(&jit_add_edge);
    e.jmp(arm7_dispatch_dynamic);
  }

  {
    e.align(32);

    arm7_dispatch_compile = e.getCurr<void *>();

    e.mov(arg0, (uint64_t)jit);
    e.mov(arg1, e.dword[e.r14 + offsetof(struct armv3_context, r[15])]);
    e.call(&jit_compile_block);
    e.jmp(arm7_dispatch_dynamic);
  }

  {
    e.align(32);

    arm7_dispatch_interrupt = e.getCurr<void *>();

    e.mov(arg0, (uint64_t)sh4);
    e.call(&arm7_check_pending_interrupts);
    e.jmp(arm7_dispatch_dynamic);
  }

  {
    e.align(32);

    arm7_dispatch_enter = e.getCurr<void (*)()>();

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
    e.sub(e.rsp, arm7_stack_size + 8);
    e.mov(e.r14, (uint64_t)ctx);
    e.mov(e.r15, (uint64_t)mem);
    e.jmp(arm7_dispatch_dynamic);
  }

  {
    e.align(32);

    arm7_dispatch_leave = e.getCurr<void *>();

    e.add(e.rsp, arm7_stack_size + 8);
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

  /* reset cache after compiling the thunks it points to */
  arm7_dispatch_reset();
}
