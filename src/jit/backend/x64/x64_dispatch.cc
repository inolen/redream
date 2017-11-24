#include "jit/backend/x64/x64_local.h"

extern "C" {
#include "core/core.h"
#include "jit/jit.h"
#include "jit/jit_guest.h"
}

/* log out pc each time dispatch is entered for debugging */
#define LOG_DISPATCH_EVERY_N 0

/* controls if edges are added and managed between static branches. the first
   time each branch is hit, its destination block will be dynamically looked
   up. if this is enabled, an edge will be added between the two blocks, and
   the branch will be patched to directly jmp to the destination block,
   avoiding the need for redundant lookups */
#define LINK_STATIC_BRANCHES !LOG_DISPATCH_EVERY_N

static inline void **x64_dispatch_code_ptr(struct x64_backend *backend,
                                           uint32_t addr) {
  return &backend->cache[(addr & backend->cache_mask) >> backend->cache_shift];
}

#if LOG_DISPATCH_EVERY_N
static void x64_dispatch_log(struct x64_ctx *ctx) {
  static uint64_t num;

  if ((num++ % LOG_DISPATCH_EVERY_N) == 0) {
    LOG_INFO("x64_log_dispatch 0x%08x", ctx->pc);
  }
}
#endif

void x64_dispatch_restore_edge(struct jit_backend *base, void *code,
                               uint32_t dst) {
  struct x64_backend *backend = container_of(base, struct x64_backend, base);

  Xbyak::CodeGenerator e(32, code);
  e.call(backend->dispatch_static);
}

void x64_dispatch_patch_edge(struct jit_backend *base, void *code, void *dst) {
  struct x64_backend *backend = container_of(base, struct x64_backend, base);

  Xbyak::CodeGenerator e(32, code);
  e.jmp(dst);
}

void x64_dispatch_invalidate_code(struct jit_backend *base, uint32_t addr) {
  struct x64_backend *backend = container_of(base, struct x64_backend, base);
  void **entry = x64_dispatch_code_ptr(backend, addr);
  *entry = backend->dispatch_compile;
}

void x64_dispatch_cache_code(struct jit_backend *base, uint32_t addr,
                             void *code) {
  struct x64_backend *backend = container_of(base, struct x64_backend, base);
  void **entry = x64_dispatch_code_ptr(backend, addr);
  CHECK_EQ(*entry, backend->dispatch_compile);
  *entry = code;
}

void *x64_dispatch_lookup_code(struct jit_backend *base, uint32_t addr) {
  struct x64_backend *backend = container_of(base, struct x64_backend, base);
  void **entry = x64_dispatch_code_ptr(backend, addr);
  return *entry;
}

void x64_dispatch_run_code(struct jit_backend *base, int cycles) {
  struct x64_backend *backend = container_of(base, struct x64_backend, base);
  backend->dispatch_enter(cycles);
}

void x64_dispatch_emit_thunks(struct x64_backend *backend) {
  struct jit_guest *guest = backend->base.guest;

  auto &e = *backend->codegen;
  int stack_offset = 0;

  /* emit dispatch thunks */
  {
    /* called after a dynamic branch instruction stores the next pc to the
       context. looks up the host block for it jumps to it */
    e.align(32);

    backend->dispatch_dynamic = e.getCurr<void *>();

#if LOG_DISPATCH_EVERY_N
    e.mov(arg0, guestctx);
    e.call(&x64_dispatch_log);
#endif

    /* invasively look into the jit's cache */
    e.mov(e.rax, (uint64_t)backend->cache);
    e.mov(e.ecx, e.dword[guestctx + guest->offset_pc]);
    e.and_(e.ecx, backend->cache_mask);
    e.jmp(e.qword[e.rax + e.rcx * (sizeof(void *) >> backend->cache_shift)]);
  }

  {
    /* called after a static branch instruction stores the next pc to the
       context. the thunk calls jit_add_edge which adds an edge between the
       calling block and the branch destination block, and then falls through
       to the above dynamic branch thunk. on the second run through this code
       jit_add_edge will call x64_dispatch_patch_edge, patching the caller to
       directly jump to the destination block */
    e.align(32);

    backend->dispatch_static = e.getCurr<void *>();

#if LINK_STATIC_BRANCHES
    e.mov(arg0, (uint64_t)guest->data);
    e.pop(arg1);
    e.sub(arg1, 5 /* sizeof jmp instr */);
    e.mov(arg2, e.qword[guestctx + guest->offset_pc]);
    e.call(guest->link_code);
#else
    e.pop(arg1);
#endif
    e.jmp(backend->dispatch_dynamic);
  }

  {
    /* default cache entry for all blocks. compiles the desired pc before
       jumping to the block through the dynamic dispatch thunk */
    e.align(32);

    backend->dispatch_compile = e.getCurr<void *>();

    e.mov(arg0, (uint64_t)guest->data);
    e.mov(arg1, e.dword[guestctx + guest->offset_pc]);
    e.call(guest->compile_code);
    e.jmp(backend->dispatch_dynamic);
  }

  {
    /* processes the pending interrupt request, and then jumps to the new pc
       through the dynamic dispatch thunk */
    e.align(32);

    backend->dispatch_interrupt = e.getCurr<void *>();

    e.mov(arg0, (uint64_t)guest->data);
    e.call(guest->check_interrupts);
    e.jmp(backend->dispatch_dynamic);
  }

  {
    /* entry point to the compiled x64 code. sets up the stack frame, sets up
       fixed registers (context and memory base) and then jumps to the current
       pc through the dynamic dispatch thunk */
    e.align(32);

    backend->dispatch_enter = e.getCurr<void (*)(int)>();

    /* create stack frame and ensure stack is 16-byte aligned. note, the stack
       is currently unaligned due to the 8-byte return address that was pushed
       when this thunk was called */
    stack_offset = x64_backend_push_regs(backend, JIT_CALLEE_SAVE);
    stack_offset = ALIGN_UP(stack_offset + X64_STACK_SIZE + 8, 16) - 8;
    e.sub(e.rsp, stack_offset);

    /* assign fixed registers */
    e.mov(guestctx, (uint64_t)guest->ctx);
    e.mov(guestmem, (uint64_t)guest->membase);

    /* reset run state */
    e.mov(e.dword[guestctx + guest->offset_cycles], arg0);
    e.mov(e.dword[guestctx + guest->offset_instrs], 0);

    e.jmp(backend->dispatch_dynamic);
  }

  {
    /* exit point for the compiled x64 code, tears down the stack frame and
       returns */
    e.align(32);

    backend->dispatch_exit = e.getCurr<void *>();

    /* destroy stack frame */
    e.add(e.rsp, stack_offset);
    x64_backend_pop_regs(backend, JIT_CALLEE_SAVE);

    e.ret();
  }

  /* reset cache entries to point to the new compile thunk */
  for (int i = 0; i < backend->cache_size; i++) {
    backend->cache[i] = backend->dispatch_compile;
  }
}

void x64_dispatch_shutdown(struct x64_backend *backend) {
  free(backend->cache);
}

void x64_dispatch_init(struct x64_backend *backend) {
  struct jit_guest *guest = backend->base.guest;

  /* initialize code cache, one entry per possible block begin */
  backend->cache_mask = guest->addr_mask;
  backend->cache_shift = ctz32(guest->addr_mask);
  backend->cache_size = (backend->cache_mask >> backend->cache_shift) + 1;
  backend->cache = (void **)malloc(backend->cache_size * sizeof(void *));
}
