#ifndef JIT_GUEST_H
#define JIT_GUEST_H

#include <stdint.h>

typedef uint32_t (*mem_read_cb)(void *, uint32_t, uint32_t);
typedef void (*mem_write_cb)(void *, uint32_t, uint32_t, uint32_t);

typedef void (*jit_compile_cb)(void *, uint32_t);
typedef void (*jit_link_cb)(void *, uint32_t);
typedef void (*jit_interrupt_cb)(void *);

struct address_space;

struct jit_guest {
  /* mask used to directly map each guest address to a block of code */
  uint32_t addr_mask;

  /* memory interface used by both the frontend and backend */
  void *ctx;
  void *mem;
  struct address_space *space;
  void (*lookup)(struct address_space *, uint32_t, void **, void **,
                 mem_read_cb *, mem_write_cb *, uint32_t *);
  uint8_t (*r8)(struct address_space *, uint32_t);
  uint16_t (*r16)(struct address_space *, uint32_t);
  uint32_t (*r32)(struct address_space *, uint32_t);
  uint64_t (*r64)(struct address_space *, uint32_t);
  void (*w8)(struct address_space *, uint32_t, uint8_t);
  void (*w16)(struct address_space *, uint32_t, uint16_t);
  void (*w32)(struct address_space *, uint32_t, uint32_t);
  void (*w64)(struct address_space *, uint32_t, uint64_t);

  /* runtime interface used by the backend and dispatch */
  void *data;
  int offset_pc;
  int offset_cycles;
  int offset_instrs;
  int offset_interrupts;
  jit_compile_cb compile_code;
  jit_link_cb link_code;
  jit_interrupt_cb check_interrupts;
};

#endif
