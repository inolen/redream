#ifndef ARMV3_GUEST_H
#define ARMV3_GUEST_H

#include "jit/jit.h"

struct armv3_guest {
  struct jit_guest;

  /* runtime interface */
  void (*switch_mode)(void *, uint32_t);
  void (*restore_mode)(void *);
  void (*software_interrupt)(void *);
};

static inline struct armv3_guest *armv3_guest_create() {
  return calloc(1, sizeof(struct armv3_guest));
}

static inline void armv3_guest_destroy(struct armv3_guest *guest) {
  free(guest);
}

#endif
