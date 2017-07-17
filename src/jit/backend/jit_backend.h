#ifndef JIT_BACKEND_H
#define JIT_BACKEND_H

#include <stdint.h>
#include "jit/ir/ir.h"

struct exception_state;
struct jit;
struct jit_block;

/* macro to help declare a code buffer for the backends to use

   note, the code buffer needs to be placed in the data segment (as opposed to
   allocating on the heap) to keep it within 2 GB of the code segment, enabling
   the x64 backend to use RIP-relative offsets when calling functions

   further, the code buffer needs to be no greater than 1 MB in size so the a64
   backend can use conditional branches to thunks without trampolining

   finally, the code buffer needs to be aligned to a 4kb page so it's easy to
   mprotect */
#if ARCH_A64
#define DEFINE_JIT_CODE_BUFFER(name) static uint8_t name[0x100000] ALIGNED(4096)
#else
#define DEFINE_JIT_CODE_BUFFER(name) static uint8_t name[0x800000] ALIGNED(4096)
#endif

enum {
  /* register is callee-saved */
  JIT_CALLEE_SAVED = 0x1,
  /* register is caller-saved */
  JIT_CALLER_SAVED = 0x2,
  /* result must contain arg0. this signals the register allocator to insert a
     copy from arg0 to result if it fails to reuse the same register for both.
     this is required by several operations, namely binary arithmetic ops on
     x64, which only take two operands */
  JIT_REUSE_ARG0 = 0x4,
  /* argument is optional */
  JIT_OPTIONAL = 0x8,
  /* argument can be in a 64-bit or less int register */
  JIT_REG_I64 = 0x10,
  /* argument can be in a 64-bit or less float register */
  JIT_REG_F64 = 0x20,
  /* argument can be in a 128-bit or less vector register */
  JIT_REG_V128 = 0x40,
  /* argument can be a 32-bit or less int immediate */
  JIT_IMM_I32 = 0x80,
  /* argument can be a 64-bit or less int immediate */
  JIT_IMM_I64 = 0x100,
  /* argument can be a 32-bit or less float immediate */
  JIT_IMM_F32 = 0x200,
  /* argument can be a 64-bit or less float immediate */
  JIT_IMM_F64 = 0x400,
  JIT_TYPE_MASK = JIT_REG_I64 | JIT_REG_F64 | JIT_REG_V128 | JIT_IMM_I32 |
                  JIT_IMM_I64 | JIT_IMM_F32 | JIT_IMM_F64,
};

/* backend-specific register definition */
struct jit_register {
  const char *name;
  int flags;
  const void *data;
};

/* backend-specific emitter definition */
struct jit_emitter {
  void *func;
  int res_flags;
  int arg_flags[IR_MAX_ARGS];
};

enum {
  JIT_ABI_DISPATCH,
  JIT_ABI_CDECL,
};

struct jit_backend {
  struct jit *jit;

  const struct jit_register *registers;
  int num_registers;

  const struct jit_emitter *emitters;
  int num_emitters;

  void (*init)(struct jit_backend *);
  void (*destroy)(struct jit_backend *);

  /* compile interface */
  void (*reset)(struct jit_backend *);
  int (*assemble_code)(struct jit_backend *, struct jit_block *, struct ir *, int abi);
  void (*dump_code)(struct jit_backend *, const struct jit_block *);
  int (*handle_exception)(struct jit_backend *, struct exception_state *);

  /* dispatch interface */
  void (*run_code)(struct jit_backend *, int);
  void *(*lookup_code)(struct jit_backend *, uint32_t);
  void (*cache_code)(struct jit_backend *, uint32_t, void *);
  void (*invalidate_code)(struct jit_backend *, uint32_t);
  void (*patch_edge)(struct jit_backend *, void *, void *);
  void (*restore_edge)(struct jit_backend *, void *, uint32_t);
};

#endif
