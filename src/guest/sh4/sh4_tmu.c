#include "guest/sh4/sh4_tmu.h"
#include "guest/scheduler.h"
#include "guest/sh4/sh4.h"
#include "imgui.h"

static const int64_t PERIPHERAL_CLOCK_FREQ = SH4_CLOCK_FREQ >> 2;
static const int PERIPHERAL_SCALE[] = {2, 4, 6, 8, 10, 0, 0, 0};

#define TSTR(n) (*sh4->TSTR & (1 << n))
#define TCOR(n) (n == 0 ? sh4->TCOR0 : n == 1 ? sh4->TCOR1 : sh4->TCOR2)
#define TCNT(n) (n == 0 ? sh4->TCNT0 : n == 1 ? sh4->TCNT1 : sh4->TCNT2)
#define TCR(n) (n == 0 ? sh4->TCR0 : n == 1 ? sh4->TCR1 : sh4->TCR2)
#define TUNI(n) \
  (n == 0 ? SH4_INT_TUNI0 : n == 1 ? SH4_INT_TUNI1 : SH4_INT_TUNI2)

static void sh4_tmu_reschedule(struct sh4 *sh4, int n, uint32_t tcnt,
                               uint32_t tcr);

static uint32_t sh4_tmu_tcnt(struct sh4 *sh4, int n) {
  struct scheduler *sched = sh4->dc->sched;

  /* TCNT values aren't updated in real time. if a timer is enabled, query
     the scheduler to figure out how many cycles are remaining for the given
     timer */
  struct timer *timer = sh4->tmu_timers[n];
  if (!timer) {
    return *TCNT(n);
  }

  /* FIXME should the number of SH4 cycles that've been executed be considered
     here? this would prevent an entire SH4 slice from just busy waiting on
     this to change */
  uint32_t tcr = *TCR(n);
  int64_t freq = PERIPHERAL_CLOCK_FREQ >> PERIPHERAL_SCALE[tcr & 7];
  int64_t remaining = sched_remaining_time(sched, timer);
  int64_t cycles = NANO_TO_CYCLES(remaining, freq);

  return (uint32_t)cycles;
}

static void sh4_tmu_expire(struct sh4 *sh4, int n) {
  uint32_t *tcor = TCOR(n);
  uint32_t *tcnt = TCNT(n);
  uint32_t *tcr = TCR(n);

#if 0
  LOG_INFO("sh4_tmu_expire %d", n);
#endif

  /* timer expired, set the underflow flag */
  *tcr |= 0x100;

  /* if interrupt generation on underflow is enabled, do so */
  if (*tcr & 0x20) {
    sh4_raise_interrupt(sh4, TUNI(n));
  }

  /* reset TCNT with the value from TCOR */
  *tcnt = *tcor;

  /* reschedule the timer with the new count */
  sh4->tmu_timers[n] = NULL;
  sh4_tmu_reschedule(sh4, n, *tcnt, *tcr);
}

static void sh4_tmu_expire_0(void *data) {
  sh4_tmu_expire(data, 0);
}

static void sh4_tmu_expire_1(void *data) {
  sh4_tmu_expire(data, 1);
}

static void sh4_tmu_expire_2(void *data) {
  sh4_tmu_expire(data, 2);
}

static void sh4_tmu_reschedule(struct sh4 *sh4, int n, uint32_t tcnt,
                               uint32_t tcr) {
  struct scheduler *sched = sh4->dc->sched;
  struct timer **timer = &sh4->tmu_timers[n];

  int64_t freq = PERIPHERAL_CLOCK_FREQ >> PERIPHERAL_SCALE[tcr & 7];
  int64_t cycles = (int64_t)tcnt;
  int64_t remaining = CYCLES_TO_NANO(cycles, freq);

  if (*timer) {
    sched_cancel_timer(sched, *timer);
    *timer = NULL;
  }

  timer_cb cb = (n == 0 ? &sh4_tmu_expire_0
                        : n == 1 ? &sh4_tmu_expire_1 : &sh4_tmu_expire_2);
  *timer = sched_start_timer(sched, cb, sh4, remaining);
}

static void sh4_tmu_update_tstr(struct sh4 *sh4) {
  struct scheduler *sched = sh4->dc->sched;

  for (int i = 0; i < 3; i++) {
    struct timer **timer = &sh4->tmu_timers[i];

    if (TSTR(i)) {
      /* schedule the timer if not already started */
      if (!*timer) {
        sh4_tmu_reschedule(sh4, i, *TCNT(i), *TCR(i));
      }
    } else if (*timer) {
      /* save off progress */
      *TCNT(i) = sh4_tmu_tcnt(sh4, i);

      /* disable the timer */
      sched_cancel_timer(sched, *timer);
      *timer = NULL;
    }
  }
}

static void sh4_tmu_update_tcr(struct sh4 *sh4, uint32_t n) {
  if (TSTR(n)) {
    /* timer is already scheduled, reschedule it with the current cycle
       count, but the new TCR value */
    sh4_tmu_reschedule(sh4, n, sh4_tmu_tcnt(sh4, n), *TCR(n));
  }

  /* if the timer no longer cares about underflow interrupts, unrequest */
  if (!(*TCR(n) & 0x20) || !(*TCR(n) & 0x100)) {
    sh4_clear_interrupt(sh4, TUNI(n));
  }
}

static void sh4_tmu_update_tcnt(struct sh4 *sh4, uint32_t n) {
  if (TSTR(n)) {
    sh4_tmu_reschedule(sh4, n, *TCNT(n), *TCR(n));
  }
}

#ifdef HAVE_IMGUI
void sh4_tmu_debug_menu(struct sh4 *sh4) {
  if (igBegin("tmu stats", NULL, 0)) {
    igColumns(6, NULL, 0);

    igText("#");
    igNextColumn();
    igText("started");
    igNextColumn();
    igText("count");
    igNextColumn();
    igText("control");
    igNextColumn();
    igText("reset count");
    igNextColumn();
    igText("underflowed");
    igNextColumn();

    for (int i = 0; i < 3; i++) {
      igText("%d", i);
      igNextColumn();
      igText(TSTR(i) ? "yes" : "no");
      igNextColumn();
      igText("0x%08x", sh4_tmu_tcnt(sh4, i));
      igNextColumn();
      igText("0x%08x", TCR(i));
      igNextColumn();
      igText("0x%08x", TCOR(i));
      igNextColumn();
      igText("0x%x", TUNI(i));
      igNextColumn();
    }

    igEnd();
  }
}
#endif

REG_W32(sh4_cb, TSTR) {
  struct sh4 *sh4 = dc->sh4;
  *sh4->TSTR = value;
  sh4_tmu_update_tstr(sh4);
}

REG_W32(sh4_cb, TCR0) {
  struct sh4 *sh4 = dc->sh4;
  *sh4->TCR0 = value;
  sh4_tmu_update_tcr(sh4, 0);
}

REG_W32(sh4_cb, TCR1) {
  struct sh4 *sh4 = dc->sh4;
  *sh4->TCR1 = value;
  sh4_tmu_update_tcr(sh4, 1);
}

REG_W32(sh4_cb, TCR2) {
  struct sh4 *sh4 = dc->sh4;
  *sh4->TCR2 = value;
  sh4_tmu_update_tcr(sh4, 2);
}

REG_R32(sh4_cb, TCNT0) {
  struct sh4 *sh4 = dc->sh4;
  return sh4_tmu_tcnt(sh4, 0);
}

REG_W32(sh4_cb, TCNT0) {
  struct sh4 *sh4 = dc->sh4;
  *sh4->TCNT0 = value;
  sh4_tmu_update_tcnt(sh4, 0);
}

REG_R32(sh4_cb, TCNT1) {
  struct sh4 *sh4 = dc->sh4;
  return sh4_tmu_tcnt(sh4, 1);
}

REG_W32(sh4_cb, TCNT1) {
  struct sh4 *sh4 = dc->sh4;
  *sh4->TCNT1 = value;
  sh4_tmu_update_tcnt(sh4, 1);
}

REG_R32(sh4_cb, TCNT2) {
  struct sh4 *sh4 = dc->sh4;
  return sh4_tmu_tcnt(sh4, 2);
}

REG_W32(sh4_cb, TCNT2) {
  struct sh4 *sh4 = dc->sh4;
  *sh4->TCNT2 = value;
  sh4_tmu_update_tcnt(sh4, 2);
}
