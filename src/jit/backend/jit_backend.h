#ifndef JIT_BACKEND_H
#define JIT_BACKEND_H

#include <stdint.h>

struct exception_state;
struct ir;
struct jit;
struct jit_block;

struct jit_register {
  const char *name;
  int value_types;
  const void *data;
};

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

struct jit_backend {
  struct jit *jit;

  const struct jit_register *registers;
  int num_registers;

  void (*init)(struct jit_backend *);
  void (*destroy)(struct jit_backend *);

  /* compile interface */
  void (*reset)(struct jit_backend *);
  int (*assemble_code)(struct jit_backend *, struct jit_block *, struct ir *);
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
