#ifndef SH4_GUEST_H
#define SH4_GUEST_H

#include "jit/jit.h"

struct sh4_guest {
  struct jit_guest;

  /* runtime interface */
  void *data;
  void (*translate)(void *, uint32_t, struct ir *, int, int *);
  void (*invalid_instr)(void *, uint32_t);
  void (*sq_prefetch)(void *, uint32_t);
  void (*sr_updated)(void *, uint32_t);
  void (*fpscr_updated)(void *, uint32_t);
};

static inline struct sh4_guest *sh4_guest_create() {
  return calloc(1, sizeof(struct sh4_guest));
}

static inline void sh4_guest_destroy(struct sh4_guest *guest) {
  free(guest);
}

#endif
