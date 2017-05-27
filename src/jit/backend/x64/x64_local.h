#ifndef X64_LOCAL_H
#define X64_LOCAL_H

#include <capstone.h>
#include <inttypes.h>

#define XBYAK_NO_OP_NAMES
#include <xbyak/xbyak.h>
#include <xbyak/xbyak_util.h>

extern "C" {
#include "jit/backend/jit_backend.h"
}

#define X64_STACK_SIZE 1024
#define X64_THUNK_SIZE 1024

enum xmm_constant {
  XMM_CONST_ABS_MASK_PS,
  XMM_CONST_ABS_MASK_PD,
  XMM_CONST_SIGN_MASK_PS,
  XMM_CONST_SIGN_MASK_PD,
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
  int num_temps;
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

extern const Xbyak::Reg64 arg0;
extern const Xbyak::Reg64 arg1;
extern const Xbyak::Reg64 arg2;
extern const Xbyak::Reg64 arg3;
extern const Xbyak::Reg64 tmp0;
extern const Xbyak::Reg64 tmp1;

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

#endif
