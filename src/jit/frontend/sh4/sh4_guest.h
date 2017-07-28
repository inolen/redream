#ifndef SH4_GUEST_H
#define SH4_GUEST_H

#include "jit/jit.h"

typedef void (*sh4_invalid_instr_cb)(void *);
typedef void (*sh4_trap_cb)(void *, uint32_t);
typedef void (*sh4_ltlb_cb)(void *);
typedef void (*sh4_pref_cb)(void *, uint32_t);
typedef void (*sh4_sleep_cb)(void *);
typedef void (*sh4_sr_updated_cb)(void *, uint32_t);
typedef void (*sh4_fpscr_updated_cb)(void *, uint32_t);

struct sh4_guest {
  struct jit_guest;

  /* runtime interface */
  sh4_invalid_instr_cb invalid_instr;
  sh4_trap_cb trap;
  sh4_ltlb_cb ltlb;
  sh4_pref_cb pref;
  sh4_sleep_cb sleep;
  sh4_sr_updated_cb sr_updated;
  sh4_fpscr_updated_cb fpscr_updated;
};

static inline struct sh4_guest *sh4_guest_create() {
  return calloc(1, sizeof(struct sh4_guest));
}

static inline void sh4_guest_destroy(struct sh4_guest *guest) {
  free(guest);
}

#endif
