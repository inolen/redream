#ifndef SH4_GUEST_H
#define SH4_GUEST_H

#include "jit/jit.h"

struct sh4_guest {
  struct jit_guest;

  /* runtime interface */
  void (*invalid_instr)(void *);
  void (*sq_prefetch)(void *, uint32_t);
  void (*sleep)(void *);
  void (*sr_updated)(void *, uint32_t);
  void (*fpscr_updated)(void *, uint32_t);
  void (*ldtlb)(void *);
};

static inline struct sh4_guest *sh4_guest_create() {
  return calloc(1, sizeof(struct sh4_guest));
}

static inline void sh4_guest_destroy(struct sh4_guest *guest) {
  free(guest);
}

#endif
