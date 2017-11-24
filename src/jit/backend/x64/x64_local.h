#ifndef X64_LOCAL_H
#define X64_LOCAL_H

#include <capstone.h>
#include <inttypes.h>

#define XBYAK_NO_OP_NAMES
#include <xbyak/xbyak.h>
#include <xbyak/xbyak_util.h>

extern "C" {
#include "jit/jit_backend.h"
}

enum xmm_constant {
  XMM_CONST_PS_ABS_MASK,
  XMM_CONST_PD_ABS_MASK,
  XMM_CONST_PS_SIGN_MASK,
  XMM_CONST_PD_SIGN_MASK,
  XMM_CONST_PD_MIN_INT32,
  XMM_CONST_PD_MAX_INT32,
  NUM_XMM_CONST,
};

struct x64_backend {
  struct jit_backend base;

  /* code cache */
  uint32_t cache_mask;
  int cache_shift;
  int cache_size;
  void **cache;

  /* codegen state */
  Xbyak::CodeGenerator *codegen;
  int use_avx;
  Xbyak::Label xmm_const[NUM_XMM_CONST];
  void *dispatch_dynamic;
  void *dispatch_static;
  void *dispatch_compile;
  void *dispatch_interrupt;
  void (*dispatch_enter)(int32_t);
  void *dispatch_exit;
  void (*load_thunk[16])();
  void (*store_thunk)();

  /* debug stats */
  csh capstone_handle;
};

/*
 * backend functionality used by emitters
 */
#define X64_THUNK_SIZE 8192
#define X64_STACK_SIZE 1024

#if PLATFORM_WINDOWS
#define X64_STACK_SHADOW_SPACE 32
#else
#define X64_STACK_SHADOW_SPACE 0
#endif

#define X64_STACK_LOCALS (X64_STACK_SHADOW_SPACE + 8)

#define X64_USE_AVX backend->use_avx

struct ir_value;

extern const Xbyak::Reg64 arg0;
extern const Xbyak::Reg64 arg1;
extern const Xbyak::Reg64 arg2;
extern const Xbyak::Reg64 arg3;
extern const Xbyak::Reg64 tmp0;
extern const Xbyak::Reg64 tmp1;
extern const Xbyak::Reg64 guestctx;
extern const Xbyak::Reg64 guestmem;

Xbyak::Reg x64_backend_reg(struct x64_backend *backend,
                           const struct ir_value *v);
Xbyak::Xmm x64_backend_xmm(struct x64_backend *backend,
                           const struct ir_value *v);
int x64_backend_push_regs(struct x64_backend *backend, int mask);
void x64_backend_pop_regs(struct x64_backend *backend, int mask);
void x64_backend_load_mem(struct x64_backend *backend,
                          const struct ir_value *dst,
                          const Xbyak::RegExp &src_exp);
void x64_backend_store_mem(struct x64_backend *backend,
                           const Xbyak::RegExp &dst_exp,
                           const struct ir_value *src);
void x64_backend_mov_value(struct x64_backend *backend, const Xbyak::Reg &dst,
                           const struct ir_value *v);
const Xbyak::Address x64_backend_xmm_constant(struct x64_backend *backend,
                                              enum xmm_constant c);
void x64_backend_block_label(char *name, size_t size, struct ir_block *block);
void x64_backend_emit_branch(struct x64_backend *backend, struct ir *ir,
                             const ir_value *target);

/*
 * dispatch
 */
void x64_dispatch_init(struct x64_backend *backend);
void x64_dispatch_shutdown(struct x64_backend *backend);
void x64_dispatch_emit_thunks(struct x64_backend *backend);
void x64_dispatch_run_code(struct jit_backend *base, int cycles);
void *x64_dispatch_lookup_code(struct jit_backend *base, uint32_t addr);
void x64_dispatch_cache_code(struct jit_backend *base, uint32_t addr,
                             void *code);
void x64_dispatch_invalidate_code(struct jit_backend *base, uint32_t addr);
void x64_dispatch_patch_edge(struct jit_backend *base, void *code, void *dst);
void x64_dispatch_restore_edge(struct jit_backend *base, void *code,
                               uint32_t dst);

/*
 * emitters
 */
typedef void (*x64_emit_cb)(struct x64_backend *, Xbyak::CodeGenerator &,
                            struct ir *, struct ir_instr *);
extern struct jit_emitter x64_emitters[IR_NUM_OPS];

#endif
