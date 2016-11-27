#include "hw/sh4/sh4.h"

struct sh4_interrupt_info {
  int intevt, default_priority, ipr, ipr_shift;
};

static struct sh4_interrupt_info sh4_interrupts[NUM_SH_INTERRUPTS] = {
#define SH4_INT(name, intevt, pri, ipr, ipr_shift) \
  {intevt, pri, ipr, ipr_shift},
#include "hw/sh4/sh4_int.inc"
#undef SH4_INT
};

void sh4_intc_update_pending(struct sh4 *sh4) {
  int min_priority = (sh4->ctx.sr & I) >> 4;
  uint64_t priority_mask =
      (sh4->ctx.sr & BL) ? 0 : ~sh4->priority_mask[min_priority];
  sh4->ctx.pending_interrupts = sh4->requested_interrupts & priority_mask;
}

int sh4_intc_check_pending(struct sh4 *sh4) {
  if (!sh4->ctx.pending_interrupts) {
    return 0;
  }

  // process the highest priority in the pending vector
  int n = 63 - clz64(sh4->ctx.pending_interrupts);
  enum sh4_interrupt intr = sh4->sorted_interrupts[n];
  struct sh4_interrupt_info *int_info = &sh4_interrupts[intr];

  *sh4->INTEVT = int_info->intevt;
  sh4->ctx.ssr = sh4->ctx.sr;
  sh4->ctx.spc = sh4->ctx.pc;
  sh4->ctx.sgr = sh4->ctx.r[15];
  sh4->ctx.sr |= (BL | MD | RB);
  sh4->ctx.pc = sh4->ctx.vbr + 0x600;
  sh4_sr_updated(sh4, sh4->ctx.ssr);

  return 1;
}

// generate a sorted set of interrupts based on their priority. these sorted
// ids are used to represent all of the currently requested interrupts as a
// simple bitmask
void sh4_intc_reprioritize(struct sh4 *sh4) {
  uint64_t old = sh4->requested_interrupts;
  sh4->requested_interrupts = 0;

  for (int i = 0, n = 0; i < 16; i++) {
    // for even priorities, give precedence to lower id interrupts
    for (int j = NUM_SH_INTERRUPTS - 1; j >= 0; j--) {
      struct sh4_interrupt_info *int_info = &sh4_interrupts[j];

      // get current priority for interrupt
      int priority = int_info->default_priority;
      if (int_info->ipr) {
        uint32_t ipr = sh4->reg[int_info->ipr];
        priority = ((ipr & 0xffff) >> int_info->ipr_shift) & 0xf;
      }

      if (priority != i) {
        continue;
      }

      bool was_requested = old & sh4->sort_id[j];

      sh4->sorted_interrupts[n] = j;
      sh4->sort_id[j] = (uint64_t)1 << n;
      n++;

      if (was_requested) {
        // rerequest with new sorted id
        sh4->requested_interrupts |= sh4->sort_id[j];
      }
    }

    // generate a mask for all interrupts up to the current priority
    sh4->priority_mask[i] = ((uint64_t)1 << n) - 1;
  }

  sh4_intc_update_pending(sh4);
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
