#include "hw/aica/aica.h"
#include "core/log.h"
#include "core/option.h"
#include "core/profiler.h"
#include "hw/aica/aica_types.h"
#include "hw/arm7/arm7.h"
#include "hw/dreamcast.h"
#include "hw/holly/holly.h"
#include "hw/memory.h"
#include "hw/scheduler.h"
#include "hw/sh4/sh4.h"

DEFINE_OPTION_INT(rtc, 0, OPTION_HIDDEN);

DEFINE_AGGREGATE_COUNTER(aica_samples);

#define AICA_NUM_CHANNELS 64
#define AICA_SAMPLE_FREQ INT64_C(44100)
#define AICA_SAMPLE_BATCH 10
#define AICA_SAMPLE_SHIFT 10
#define AICA_TIMER_PERIOD 0xff

struct aica_channel {
  struct channel_data *data;

  int active;

  /* signals the the current channel has looped */
  int looped;

  /* base address in host memory of sound data */
  uint8_t *base;

  uint32_t step;
  uint32_t offset;
};

struct aica {
  struct device;
  uint8_t reg[0x11000];
  uint8_t *wave_ram;

  /* reset state */
  int arm_resetting;

  /* interrupts */
  uint32_t arm_irq_l;
  uint32_t arm_irq_m;

  /* timers */
  struct timer *timers[3];

  /* real-time clock */
  struct timer *rtc_timer;
  int rtc_write;
  uint32_t rtc;

  /* channels */
  struct common_data *common_data;
  struct aica_channel channels[AICA_NUM_CHANNELS];
  struct timer *sample_timer;
};

static void aica_raise_interrupt(struct aica *aica, int intr) {
  aica->common_data->MCIPD |= (1 << intr);
  aica->common_data->SCIPD |= (1 << intr);
}

static void aica_clear_interrupt(struct aica *aica, int intr) {
  aica->common_data->MCIPD &= ~(1 << intr);
  aica->common_data->SCIPD &= ~(1 << intr);
}

static uint32_t aica_encode_arm_irq_l(struct aica *aica, uint32_t intr) {
  uint32_t l = 0;

  /* interrupts past 7 share the same bit */
  intr = MIN(intr, 7);

  if (aica->common_data->SCILV0 & (1 << intr)) {
    l |= 1;
  }

  if (aica->common_data->SCILV1 & (1 << intr)) {
    l |= 2;
  }

  if (aica->common_data->SCILV2 & (1 << intr)) {
    l |= 4;
  }

  return l;
}

static void aica_update_arm(struct aica *aica) {
  uint32_t enabled_intr = aica->common_data->SCIEB;
  uint32_t pending_intr = aica->common_data->SCIPD & enabled_intr;

  aica->arm_irq_l = 0;

  if (pending_intr) {
    for (uint32_t i = 0; i < NUM_AICA_INT; i++) {
      if (pending_intr & (1 << i)) {
        aica->arm_irq_l = aica_encode_arm_irq_l(aica, i);
        break;
      }
    }
  }

  if (aica->arm_irq_l) {
    /* FIQ handler will load L from common data to check interrupt type */
    arm7_raise_interrupt(aica->arm, ARM7_INT_FIQ);
  }
}

static void aica_update_sh(struct aica *aica) {
  uint32_t enabled_intr = aica->common_data->MCIEB;
  uint32_t pending_intr = aica->common_data->MCIPD & enabled_intr;

  if (pending_intr) {
    holly_raise_interrupt(aica->holly, HOLLY_INTC_G2AICINT);
  } else {
    holly_clear_interrupt(aica->holly, HOLLY_INTC_G2AICINT);
  }
}

static void aica_timer_reschedule(struct aica *aica, int n, uint32_t period);

static void aica_timer_expire(struct aica *aica, int n) {
  /* reschedule timer as soon as it expires */
  aica->timers[n] = NULL;
  aica_timer_reschedule(aica, n, AICA_TIMER_PERIOD);

  /* raise timer interrupt */
  static int timer_intr[3] = {AICA_INT_TIMER_A, AICA_INT_TIMER_B,
                              AICA_INT_TIMER_C};
  aica_raise_interrupt(aica, timer_intr[n]);
}

static void aica_timer_expire_0(void *data) {
  aica_timer_expire(data, 0);
}

static void aica_timer_expire_1(void *data) {
  aica_timer_expire(data, 1);
}

static void aica_timer_expire_2(void *data) {
  aica_timer_expire(data, 2);
}

static uint32_t aica_timer_tctl(struct aica *aica, int n) {
  return n == 0 ? aica->common_data->TACTL : n == 1 ? aica->common_data->TBCTL
                                                    : aica->common_data->TCCTL;
}

static uint32_t aica_timer_tcnt(struct aica *aica, int n) {
  struct timer *timer = aica->timers[n];
  if (!timer) {
    /* if no timer has been created, return the raw value */
    return n == 0 ? aica->common_data->TIMA : n == 1 ? aica->common_data->TIMB
                                                     : aica->common_data->TIMC;
  }

  /* else, dynamically compute the value based on the timer's remaining time */
  int tctl = aica_timer_tctl(aica, n);
  int64_t freq = AICA_SAMPLE_FREQ >> tctl;
  int64_t remaining = scheduler_remaining_time(aica->scheduler, timer);
  int64_t cycles = NANO_TO_CYCLES(remaining, freq);
  return (uint32_t)cycles;
}

static void aica_timer_reschedule(struct aica *aica, int n, uint32_t period) {
  struct timer **timer = &aica->timers[n];

  int64_t freq = AICA_SAMPLE_FREQ >> aica_timer_tctl(aica, n);
  int64_t cycles = (int64_t)period;
  int64_t remaining = CYCLES_TO_NANO(cycles, freq);

  if (*timer) {
    scheduler_cancel_timer(aica->scheduler, *timer);
    *timer = NULL;
  }

  static timer_cb timer_cbs[3] = {&aica_timer_expire_0, &aica_timer_expire_1,
                                  &aica_timer_expire_2};
  *timer =
      scheduler_start_timer(aica->scheduler, timer_cbs[n], aica, remaining);
}

static void aica_timer_shutdown(struct aica *aica) {
  for (int i = 0; i < 3; i++) {
    scheduler_cancel_timer(aica->scheduler, aica->timers[i]);
  }
}

static void aica_timer_init(struct aica *aica) {
  for (int i = 0; i < 3; i++) {
    aica_timer_reschedule(aica, i, AICA_TIMER_PERIOD);
  }
}

static uint32_t aica_rtc_reg_read(struct aica *aica, uint32_t addr,
                                  uint32_t data_mask) {
  switch (addr) {
    case 0x0:
      return aica->rtc >> 16;
    case 0x4:
      return aica->rtc & 0xffff;
    case 0x8:
      return 0;
    default:
      CHECK(false);
      return 0;
  }
}

static void aica_rtc_reg_write(struct aica *aica, uint32_t addr, uint32_t data,
                               uint32_t data_mask) {
  switch (addr) {
    case 0x0:
      if (aica->rtc_write) {
        aica->rtc = (data << 16) | (aica->rtc & 0xffff);
        aica->rtc_write = 0;
      }
      break;
    case 0x4:
      if (aica->rtc_write) {
        aica->rtc = (aica->rtc & 0xffff0000) | (data & 0xffff);
      }
      break;
    case 0x8:
      aica->rtc_write = data & 1;
      break;
    default:
      CHECK(false);
      break;
  }
}

static void aica_rtc_timer(void *data) {
  struct aica *aica = data;
  aica->rtc++;
  aica->rtc_timer =
      scheduler_start_timer(aica->scheduler, &aica_rtc_timer, aica, NS_PER_SEC);
}

static void aica_rtc_shutdown(struct aica *aica) {
  scheduler_cancel_timer(aica->scheduler, aica->rtc_timer);

  /* persist clock */
  OPTION_rtc = *(int *)&aica->rtc;
}

static void aica_rtc_init(struct aica *aica) {
  /* seed clock from persistant options */
  CHECK(sizeof(aica->rtc) <= sizeof(OPTION_rtc));
  aica->rtc = *(uint32_t *)&OPTION_rtc;

  /* increment clock every second */
  aica->rtc_timer =
      scheduler_start_timer(aica->scheduler, &aica_rtc_timer, aica, NS_PER_SEC);
}

static uint32_t aica_channel_step(struct aica_channel *ch) {
  uint32_t oct = ch->data->OCT;
  uint32_t step = (1 << AICA_SAMPLE_SHIFT) | ch->data->FNS;

  /* OCT ranges from -8 to +7 */
  if (oct & 8) {
    step >>= (16 - oct);
  } else {
    step <<= oct;
  }

  return step;
}

static void aica_channel_start(struct aica *aica, struct aica_channel *ch) {
  if (ch->active) {
    return;
  }

  uint32_t start_addr = (ch->data->SA_hi << 16) | ch->data->SA_lo;
  ch->active = 1;
  ch->base = &aica->wave_ram[start_addr];
  ch->step = aica_channel_step(ch);
  ch->offset = 0;

  LOG_INFO("aica_channel_start %d", ch - aica->channels);
}

static void aica_channel_stop(struct aica *aica, struct aica_channel *ch) {
  if (!ch->active) {
    return;
  }

  ch->active = 0;

  LOG_INFO("aica_channel_stop %d", ch - aica->channels);
}

static void aica_channel_update_key_state(struct aica *aica,
                                          struct aica_channel *ch) {
  if (!ch->data->KYONEX) {
    return;
  }

  /* modifying KYONEX for any channel will update the key state for all */
  for (int i = 0; i < AICA_NUM_CHANNELS; i++) {
    struct aica_channel *ch2 = &aica->channels[i];

    if (ch2->data->KYONB) {
      aica_channel_start(aica, ch2);
    } else {
      aica_channel_stop(aica, ch2);
    }
  }

  /* register is read only */
  ch->data->KYONEX = 0;
}

static void aica_channel_update(struct aica *aica, struct aica_channel *ch) {
  if (!ch->active) {
    return;
  }

  ch->offset += ch->step;

  uint32_t ca = ch->offset >> AICA_SAMPLE_SHIFT;
  if (ca > ch->data->LEA) {
    if (ch->data->LPCTL) {
      LOG_INFO("aica_channel_step %d restart", ch - aica->channels);
      ch->offset = ch->data->LSA << AICA_SAMPLE_SHIFT;
      ch->looped = 1;
    } else {
      aica_channel_stop(aica, ch);
    }
  }
}

static void aica_generate_samples(struct aica *aica, int samples) {
  for (int i = 0; i < samples; i++) {
    for (int j = 0; j < AICA_NUM_CHANNELS; j++) {
      struct aica_channel *ch = &aica->channels[j];
      aica_channel_update(aica, ch);
    }
  }

  prof_counter_add(COUNTER_aica_samples, samples);
}

static uint32_t aica_channel_reg_read(struct aica *aica, uint32_t addr,
                                      uint32_t data_mask) {
  int n = addr >> 7;
  addr &= 0x7f;
  struct aica_channel *ch = &aica->channels[n];

  return READ_DATA((uint8_t *)ch->data + addr);
}

static void aica_channel_reg_write(struct aica *aica, uint32_t addr,
                                   uint32_t data, uint32_t data_mask) {
  int n = addr >> 7;
  addr &= 0x7f;
  struct aica_channel *ch = &aica->channels[n];

  WRITE_DATA((uint8_t *)ch->data + addr);

  switch (addr) {
    case 0x0: /* SA_hi, KYONB, SA_lo */
    case 0x1:
    case 0x4:
      aica_channel_update_key_state(aica, ch);
      break;
  }
}

static uint32_t aica_common_reg_read(struct aica *aica, uint32_t addr,
                                     uint32_t data_mask) {
  switch (addr) {
    case 0x10:
    case 0x11: { /* EG, SGC, LP */
      if ((DATA_SIZE() == 2 && addr == 0x10) ||
          (DATA_SIZE() == 1 && addr == 0x11)) {
        struct aica_channel *ch = &aica->channels[aica->common_data->MSLC];
        aica->common_data->LP = ch->looped;
        ch->looped = 0;
      }
    } break;
    case 0x14: { /* CA */
      struct aica_channel *ch = &aica->channels[aica->common_data->MSLC];
      aica->common_data->CA = ch->offset >> AICA_SAMPLE_SHIFT;
    } break;
    case 0x90: { /* TIMA */
      aica->common_data->TIMA =
          (aica_timer_tctl(aica, 0) << 8) | aica_timer_tcnt(aica, 0);
    } break;
    case 0x94: { /* TIMB */
      aica->common_data->TIMB =
          (aica_timer_tctl(aica, 1) << 8) | aica_timer_tcnt(aica, 1);
    } break;
    case 0x98: { /* TIMC */
      aica->common_data->TIMC =
          (aica_timer_tctl(aica, 2) << 8) | aica_timer_tcnt(aica, 2);
    } break;
    case 0x500: { /* L0-9 */
      aica->common_data->L = aica->arm_irq_l;
    } break;
    case 0x504: { /* M0-9 */
      aica->common_data->M = aica->arm_irq_m;
    } break;
  }

  return READ_DATA((uint8_t *)aica->common_data + addr);
}

static void aica_common_reg_write(struct aica *aica, uint32_t addr,
                                  uint32_t data, uint32_t data_mask) {
  WRITE_DATA((uint8_t *)aica->common_data + addr);

  switch (addr) {
    case 0x90: { /* TIMA */
      aica_timer_reschedule(aica, 0, AICA_TIMER_PERIOD - data);
    } break;
    case 0x94: { /* TIMB */
      aica_timer_reschedule(aica, 1, AICA_TIMER_PERIOD - data);
    } break;
    case 0x98: { /* TIMC */
      aica_timer_reschedule(aica, 2, AICA_TIMER_PERIOD - data);
    } break;
    case 0x9c:
    case 0x9d:
    case 0xa0:
    case 0xa1: { /* SCIEB, SCIPD */
      aica_update_arm(aica);
    } break;
    case 0xa4:
    case 0xa5: { /* SCIRE */
      aica->common_data->SCIPD &= ~aica->common_data->SCIRE;
      aica->common_data->SCIRE = 0;
      aica_update_arm(aica);
    } break;
    case 0xb4:
    case 0xb5:
    case 0xb8:
    case 0xb9: { /* MCIEB, MCIPD */
      aica_update_sh(aica);
    } break;
    case 0xbc:
    case 0xbd: { /* MCIRE */
      aica->common_data->MCIPD &= ~aica->common_data->MCIRE;
      aica->common_data->MCIRE = 0;
      aica_update_sh(aica);
    } break;
    case 0x400: { /* ARMRST */
      if (aica->common_data->ARMRST) {
        /* suspend arm when reset is pulled low */
        aica->arm_resetting = 1;
        arm7_suspend(aica->arm);
      } else if (aica->arm_resetting) {
        /* reset and resume arm when reset is released */
        aica->arm_resetting = 0;
        arm7_reset(aica->arm);
      }
    } break;
    case 0x500: { /* L0-9 */
      /* nop */
    } break;
    case 0x504: { /* M0-9 */
      /* TODO run interrupt callbacks? */
      aica->arm_irq_m = data;
    } break;
  }
}

uint32_t aica_reg_read(struct aica *aica, uint32_t addr, uint32_t data_mask) {
  if (addr < 0x2000) {
    return aica_channel_reg_read(aica, addr, data_mask);
  } else if (addr >= 0x2800 && addr < 0x2d08) {
    return aica_common_reg_read(aica, addr - 0x2800, data_mask);
  } else if (addr >= 0x10000 && addr < 0x1000c) {
    return aica_rtc_reg_read(aica, addr - 0x10000, data_mask);
  }
  return READ_DATA(&aica->reg[addr]);
}

void aica_reg_write(struct aica *aica, uint32_t addr, uint32_t data,
                    uint32_t data_mask) {
  if (addr < 0x2000) {
    aica_channel_reg_write(aica, addr, data, data_mask);
    return;
  } else if (addr >= 0x2800 && addr < 0x2d08) {
    aica_common_reg_write(aica, addr - 0x2800, data, data_mask);
    return;
  } else if (addr >= 0x10000 && addr < 0x1000c) {
    aica_rtc_reg_write(aica, addr - 0x10000, data, data_mask);
    return;
  }

  WRITE_DATA(&aica->reg[addr]);
}

static void aica_next_sample(void *data) {
  struct aica *aica = data;

  aica_generate_samples(aica, AICA_SAMPLE_BATCH);
  aica_raise_interrupt(aica, AICA_INT_SAMPLE);
  aica_update_arm(aica);
  aica_update_sh(aica);

  /* reschedule */
  aica->sample_timer =
      scheduler_start_timer(aica->scheduler, &aica_next_sample, aica,
                            HZ_TO_NANO(AICA_SAMPLE_FREQ / AICA_SAMPLE_BATCH));
}

static bool aica_init(struct device *dev) {
  struct aica *aica = (struct aica *)dev;

  aica->wave_ram = memory_translate(aica->memory, "aica wave ram", 0x00000000);

  /* setup channel data aliases */
  for (int i = 0; i < AICA_NUM_CHANNELS; i++) {
    struct aica_channel *ch = &aica->channels[i];
    ch->data =
        (struct channel_data *)(aica->reg + sizeof(struct channel_data) * i);
  }
  aica->common_data = (struct common_data *)(aica->reg + 0x2800);

  aica_timer_init(aica);
  aica_rtc_init(aica);

  aica->sample_timer =
      scheduler_start_timer(aica->scheduler, &aica_next_sample, aica,
                            HZ_TO_NANO(AICA_SAMPLE_FREQ / AICA_SAMPLE_BATCH));

  return true;
}

void aica_destroy(struct aica *aica) {
  scheduler_cancel_timer(aica->scheduler, aica->sample_timer);
  aica_rtc_shutdown(aica);
  aica_timer_shutdown(aica);

  dc_destroy_device((struct device *)aica);
}

struct aica *aica_create(struct dreamcast *dc) {
  struct aica *aica =
      dc_create_device(dc, sizeof(struct aica), "aica", &aica_init);
  return aica;
}

/* clang-format off */
AM_BEGIN(struct aica, aica_reg_map);
  /* over-allocate to align with the host allocation granularity */
  AM_RANGE(0x00000000, 0x00010fff) AM_HANDLE("aica reg",
                                             (mmio_read_cb)&aica_reg_read,
                                             (mmio_write_cb)&aica_reg_write,
                                             NULL, NULL)
AM_END();

AM_BEGIN(struct aica, aica_data_map);
  AM_RANGE(0x00000000, 0x007fffff) AM_MOUNT("aica wave ram")
AM_END();
/* clang-format on */
