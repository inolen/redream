#include "guest/sh4/sh4.h"

/* generate a sorted set of interrupts based on their priority. these sorted
   ids are used to represent all of the currently requested interrupts as a
   simple bitmask */
void sh4_intc_reprioritize(struct sh4 *sh4) {
  uint64_t old = sh4->requested_interrupts;
  sh4->requested_interrupts = 0;

  int n = 0;

  for (int level = 0; level < 16; level++) {
    /* iterate backwards, giving priority to lower id interrupts when the
       priorities are equal */
    for (int i = SH4_NUM_INTERRUPTS - 1; i >= 0; i--) {
      struct sh4_interrupt_info *int_info = &sh4_interrupts[i];

      /* get current priority for interrupt */
      int priority = int_info->default_priority;
      if (int_info->ipr) {
        uint32_t ipr = sh4->reg[int_info->ipr];
        priority = ((ipr & 0xffff) >> int_info->ipr_shift) & 0xf;
      }

      if (priority != level) {
        continue;
      }

      uint64_t old_sort_id = sh4->sort_id[i];
      int was_requested = old_sort_id && (old & old_sort_id) == old_sort_id;

      sh4->sorted_interrupts[n] = i;
      sh4->sort_id[i] = UINT64_C(1) << n;
      n++;

      if (was_requested) {
        /* rerequest with new sorted id */
        sh4->requested_interrupts |= sh4->sort_id[i];
      }
    }

    /* generate a mask for all interrupts up to the current priority */
    sh4->priority_mask[level] = (UINT64_C(1) << n) - 1;
  }

  sh4_intc_update_pending(sh4);
}

void sh4_intc_update_pending(struct sh4 *sh4) {
  int min_priority = (sh4->ctx.sr & I_MASK) >> I_BIT;
  uint64_t mask = ~sh4->priority_mask[min_priority];
  int block = (sh4->ctx.sr & BL_MASK) == BL_MASK;

  /* ignore block bit when sleeping */
  if (sh4->ctx.sleep_mode) {
    block = 0;
  }

  /* mask all interrupts if interrupt block bit is set */
  if (block) {
    mask = 0;
  }

  sh4->ctx.pending_interrupts = sh4->requested_interrupts & mask;
}

REG_W32(sh4_cb, IPRA) {
  struct sh4 *sh4 = dc->sh4;
  *sh4->IPRA = value;
  sh4_intc_reprioritize(sh4);
}

REG_W32(sh4_cb, IPRB) {
  struct sh4 *sh4 = dc->sh4;
  *sh4->IPRB = value;
  sh4_intc_reprioritize(sh4);
}

REG_W32(sh4_cb, IPRC) {
  struct sh4 *sh4 = dc->sh4;
  *sh4->IPRC = value;
  sh4_intc_reprioritize(sh4);
}
