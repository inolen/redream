#ifndef JIT_BACKEND_H
#define JIT_BACKEND_H

#include <stdint.h>
#include <stdio.h>
#include "jit/ir/ir.h"

struct exception_state;
struct jit_block;
struct jit_guest;

/* macro to help declare a code buffer for the backends to use

   note, the code buffer needs to be placed in the data segment (as opposed to
   allocating on the heap) to keep it within 2 GB of the code segment, enabling
   the x64 backend to use RIP-relative offsets when calling functions

   further, the code buffer needs to be no greater than 1 MB in size so the a64
   backend can use conditional branches to thunks without trampolining

   finally, the code buffer needs to be aligned to a 4kb page so it's easy to
   mprotect */
#if ARCH_A64
#define DEFINE_JIT_CODE_BUFFER(name) static uint8_t ALIGNED(4096) name[0x100000]
#else
#define DEFINE_JIT_CODE_BUFFER(name) static uint8_t ALIGNED(4096) name[0x800000]
#endif

enum {
  /* allocate to this register */
  JIT_ALLOCATE = 0x1,
  /* don't allocate to this register */
  JIT_RESERVED = 0x2,
  /* register is callee-saved */
  JIT_CALLEE_SAVE = 0x4,
  /* register is caller-saved */
  JIT_CALLER_SAVE = 0x8,
  /* result must contain arg0. this signals the register allocator to insert a
     copy from arg0 to result if it fails to reuse the same register for both.
     this is required by several operations, namely binary arithmetic ops on
     x64, which only take two operands */
  JIT_REUSE_ARG0 = 0x10,
  /* argument is optional */
  JIT_OPTIONAL = 0x20,
  /* argument can be in a 64-bit or less int register */
  JIT_REG_I64 = 0x40,
  /* argument can be in a 64-bit or less float register */
  JIT_REG_F64 = 0x80,
  /* argument can be in a 128-bit or less vector register */
  JIT_REG_V128 = 0x100,
  /* argument can be a 32-bit or less int immediate */
  JIT_IMM_I32 = 0x200,
  /* argument can be a 64-bit or less int immediate */
  JIT_IMM_I64 = 0x400,
  /* argument can be a 32-bit or less float immediate */
  JIT_IMM_F32 = 0x800,
  /* argument can be a 64-bit or less float immediate */
  JIT_IMM_F64 = 0x1000,
  /* argument can be a block reference */
  JIT_IMM_BLK = 0x2000,
  JIT_TYPE_MASK = JIT_REG_I64 | JIT_REG_F64 | JIT_REG_V128 | JIT_IMM_I32 |
                  JIT_IMM_I64 | JIT_IMM_F32 | JIT_IMM_F64 | JIT_IMM_BLK,
};

/* the assemble_code function is passed this callback to map guest blocks and
   instructions to host addresses */
enum {
  JIT_EMIT_BLOCK,
  JIT_EMIT_INSTR,
};

typedef void (*jit_emit_cb)(void *, int, uint32_t, uint8_t *);

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

struct jit_backend {
  struct jit_guest *guest;

  const struct jit_register *registers;
  int num_registers;

  const struct jit_emitter *emitters;
  int num_emitters;

  void (*destroy)(struct jit_backend *);

  /* compile interface */
  void (*reset)(struct jit_backend *);
  int (*assemble_code)(struct jit_backend *, struct ir *, uint8_t **, int *,
                       jit_emit_cb, void *);
  void (*dump_code)(struct jit_backend *, const uint8_t *, int, FILE *);
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
