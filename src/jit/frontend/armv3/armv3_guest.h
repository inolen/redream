#ifndef ARMV3_GUEST_H
#define ARMV3_GUEST_H

#include <stdlib.h>
#include "jit/jit_guest.h"

typedef void (*armv3_switch_mode_cb)(void *, uint32_t);
typedef void (*armv3_restore_mode_cb)(void *);

struct armv3_guest {
  struct jit_guest;

  /* runtime interface */
  armv3_switch_mode_cb switch_mode;
  armv3_restore_mode_cb restore_mode;
};

#endif
