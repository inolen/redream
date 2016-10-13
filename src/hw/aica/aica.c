#include "hw/aica/aica.h"
#include "core/log.h"
#include "core/option.h"
#include "hw/aica/aica_types.h"
#include "hw/arm/arm.h"
#include "hw/dreamcast.h"
#include "hw/holly/holly.h"
#include "hw/memory.h"
#include "hw/scheduler.h"
#include "hw/sh4/sh4.h"

DEFINE_OPTION_INT(rtc, 0, OPTION_HIDDEN);

#define AICA_CLOCK_FREQ INT64_C(22579200)
#define AICA_SAMPLE_FREQ INT64_C(44100)
#define AICA_NUM_CHANNELS 64
#define AICA_TIMER_PERIOD 0xff

struct aica_channel {
  struct channel_data *data;
  uint8_t *start;
};

struct aica {
  struct device;
  uint8_t *reg_ram;
  uint8_t *wave_ram;
  // timers
  struct timer *timers[3];
  // real-time clock
  struct timer *rtc_timer;
  int rtc_write;
  uint32_t rtc;
  // channels
  struct common_data *common_data;
  struct aica_channel channels[AICA_NUM_CHANNELS];
};

/*
 * interrupts
 */
static void aica_raise_interrupt(struct aica *aica, int intr) {
  aica->common_data->MCIPD |= intr;
  aica->common_data->SCIPD |= intr;
}

static void aica_clear_interrupt(struct aica *aica, int intr) {
  aica->common_data->MCIPD &= ~intr;
  aica->common_data->SCIPD &= ~intr;
}

static void aica_update_arm(struct aica *aica) {}

static void aica_update_sh(struct aica *aica) {
  uint32_t enabled_intr = aica->common_data->MCIEB;
  uint32_t pending_intr = aica->common_data->MCIPD & enabled_intr;

  if (pending_intr) {
    holly_toggle_interrupt(aica->holly, HOLLY_INTC_G2AICINT);
  }
}

/*
 * timers
 */
static void aica_timer_reschedule(struct aica *aica, int n, uint32_t period);

static void aica_timer_expire(struct aica *aica, int n) {
  // reschedule timer as soon as it expires
  aica->timers[n] = NULL;
  aica_timer_reschedule(aica, n, AICA_TIMER_PERIOD);

  // raise timer interrupt
  static holly_interrupt_t timer_intr[3] = {AICA_INT_TIMER_A, AICA_INT_TIMER_B,
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
    // if no timer has been created, return the raw value
    return n == 0 ? aica->common_data->TIMA : n == 1 ? aica->common_data->TIMB
                                                     : aica->common_data->TIMC;
  }

  // else, dynamically compute the value based on the timer's remaining time
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

/*
 * rtc
 */
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

  // persist clock
  OPTION_rtc = *(int *)&aica->rtc;
}

static void aica_rtc_init(struct aica *aica) {
  // seed clock from persistant options
  CHECK(sizeof(aica->rtc) <= sizeof(OPTION_rtc));
  aica->rtc = *(uint32_t *)&OPTION_rtc;

  // increment clock every second
  aica->rtc_timer =
      scheduler_start_timer(aica->scheduler, &aica_rtc_timer, aica, NS_PER_SEC);
}

/*
 * channels
 */
static void aica_channel_start(struct aica *aica, struct aica_channel *ch) {
  CHECK_EQ(ch->data->KYONB, 1);

  uint32_t start_addr = (ch->data->SA_hi << 16) | ch->data->SA_lo;
  ch->start = &aica->wave_ram[start_addr];
}

static void aica_channel_stop(struct aica *aica, struct aica_channel *ch) {
  CHECK_EQ(ch->data->KYONB, 0);
}

static void aica_channel_update_key_state(struct aica *aica,
                                          struct aica_channel *ch) {
  if (!ch->data->KYONEX) {
    return;
  }

  // modifying KYONEX for any channel will update the key state for all channels
  for (int i = 0; i < AICA_NUM_CHANNELS; i++) {
    struct aica_channel *ch2 = &aica->channels[i];

    if (ch2->data->KYONB) {
      aica_channel_start(aica, ch2);
    } else {
      aica_channel_stop(aica, ch2);
    }
  }

  // register is read only
  ch->data->KYONEX = 0;
}

static void aica_step_channel(struct aica *aica, struct aica_channel *ch) {
  if (!ch->data->KYONB) {
    return;
  }

  LOG_FATAL("STEP CHANNEL %d", ch - aica->channels);
}

static void aica_generate_samples(struct aica *aica, int samples) {
  for (int i = 0; i < samples; i++) {
    for (int j = 0; j < AICA_NUM_CHANNELS; j++) {
      struct aica_channel *ch = &aica->channels[j];

      aica_step_channel(aica, ch);
    }
  }
}

static uint32_t aica_channel_reg_read(struct aica *aica, uint32_t addr,
                                      uint32_t data_mask) {
  int n = addr >> 7;
  addr &= 0x7f;
  struct aica_channel *ch = &aica->channels[n];

  return READ_DATA(&ch->data[addr]);
}

static void aica_channel_reg_write(struct aica *aica, uint32_t addr,
                                   uint32_t data, uint32_t data_mask) {
  int n = addr >> 7;
  addr &= 0x7f;
  struct aica_channel *ch = &aica->channels[n];

  WRITE_DATA(&ch->data[addr]);

  switch (addr) {
    case 0x0:
    case 0x1:
      aica_channel_update_key_state(aica, ch);
      break;
  }
}

static uint32_t aica_common_reg_read(struct aica *aica, uint32_t addr,
                                     uint32_t data_mask) {
  switch (addr) {
    case 0x90: /* TIMA */
      return (aica_timer_tctl(aica, 0) << 8) | aica_timer_tcnt(aica, 0);
    case 0x94: /* TIMB */
      return (aica_timer_tctl(aica, 1) << 8) | aica_timer_tcnt(aica, 1);
    case 0x98: /* TIMC */
      return (aica_timer_tctl(aica, 2) << 8) | aica_timer_tcnt(aica, 2);
  }
  return READ_DATA(&aica->reg_ram[0x2800 + addr]);
}

static void aica_common_reg_write(struct aica *aica, uint32_t addr,
                                  uint32_t data, uint32_t data_mask) {
  WRITE_DATA(&aica->reg_ram[addr]);

  switch (addr) {
    case 0x90: { /* TIMA */
      aica_timer_reschedule(aica, 0,
                            AICA_TIMER_PERIOD - aica_timer_tcnt(aica, 0));
    } break;
    case 0x94: { /* TIMB */
      aica_timer_reschedule(aica, 1,
                            AICA_TIMER_PERIOD - aica_timer_tcnt(aica, 1));
    } break;
    case 0x98: { /* TIMC */
      aica_timer_reschedule(aica, 2,
                            AICA_TIMER_PERIOD - aica_timer_tcnt(aica, 2));
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
      if (data) {
        arm_suspend(aica->arm);
      } else {
        arm_resume(aica->arm);
      }
    } break;
  }
}

/*
 * memory callbacks
 */
uint32_t aica_reg_read(struct aica *aica, uint32_t addr, uint32_t data_mask) {
  if (addr < 0x2000) {
    return aica_channel_reg_read(aica, addr, data_mask);
  } else if (addr >= 0x2800 && addr < 0x2d08) {
    return aica_common_reg_read(aica, addr - 0x2800, data_mask);
  } else if (addr >= 0x10000 && addr < 0x1000c) {
    return aica_rtc_reg_read(aica, addr - 0x10000, data_mask);
  }
  return READ_DATA(&aica->reg_ram[addr]);
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

  WRITE_DATA(&aica->reg_ram[addr]);
}

uint32_t aica_wave_read(struct aica *aica, uint32_t addr, uint32_t data_mask) {
  if (DATA_SIZE() == 4) {
    // FIXME temp hacks to get Crazy Taxi 1 booting
    if (addr == 0x104 || addr == 0x284 || addr == 0x288) {
      return 0x54494e49;
    }
    // FIXME temp hacks to get Crazy Taxi 2 booting
    if (addr == 0x5c) {
      return 0x54494e49;
    }
    // FIXME temp hacks to get PoP booting
    if (addr == 0xb200 || addr == 0xb210 || addr == 0xb220 || addr == 0xb230 ||
        addr == 0xb240 || addr == 0xb250 || addr == 0xb260 || addr == 0xb270 ||
        addr == 0xb280 || addr == 0xb290 || addr == 0xb2a0 || addr == 0xb2b0 ||
        addr == 0xb2c0 || addr == 0xb2d0 || addr == 0xb2e0 || addr == 0xb2f0 ||
        addr == 0xb300 || addr == 0xb310 || addr == 0xb320 || addr == 0xb330 ||
        addr == 0xb340 || addr == 0xb350 || addr == 0xb360 || addr == 0xb370 ||
        addr == 0xb380 || addr == 0xb390 || addr == 0xb3a0 || addr == 0xb3b0 ||
        addr == 0xb3c0 || addr == 0xb3d0 || addr == 0xb3e0 || addr == 0xb3f0) {
      return 0x0;
    }
  }

  return READ_DATA(&aica->wave_ram[addr]);
}

void aica_wave_write(struct aica *aica, uint32_t addr, uint32_t data,
                     uint32_t data_mask) {
  WRITE_DATA(&aica->wave_ram[addr]);
}

/*
 * device
 */
static void aica_run(struct device *dev, int64_t ns) {
  struct aica *aica = (struct aica *)dev;

  int64_t cycles = MAX(NANO_TO_CYCLES(ns, AICA_CLOCK_FREQ), INT64_C(1));
  int64_t samples = NANO_TO_CYCLES(ns, AICA_SAMPLE_FREQ);

  aica_generate_samples(aica, samples);

  aica_raise_interrupt(aica, AICA_INT_SAMPLE);

  aica_update_arm(aica);
  aica_update_sh(aica);
}

static bool aica_init(struct device *dev) {
  struct aica *aica = (struct aica *)dev;

  aica->reg_ram = memory_translate(aica->memory, "aica reg ram", 0x00000000);
  aica->wave_ram = memory_translate(aica->memory, "aica wave ram", 0x00000000);

  // setup channel data aliases
  for (int i = 0; i < AICA_NUM_CHANNELS; i++) {
    struct aica_channel *ch = &aica->channels[i];
    ch->data = (struct channel_data *)(aica->reg_ram +
                                       sizeof(struct channel_data) * i);
  }
  aica->common_data = (struct common_data *)(aica->reg_ram + 0x2800);

  aica_timer_init(aica);
  aica_rtc_init(aica);

  arm_suspend(aica->arm);

  return true;
}

struct aica *aica_create(struct dreamcast *dc) {
  struct aica *aica =
      dc_create_device(dc, sizeof(struct aica), "aica", &aica_init);

  aica->execute_if = dc_create_execute_interface(&aica_run);

  return aica;
}

void aica_destroy(struct aica *aica) {
  aica_rtc_shutdown(aica);
  aica_timer_shutdown(aica);

  dc_destroy_execute_interface(aica->execute_if);
  dc_destroy_device((struct device *)aica);
}

// clang-format off
AM_BEGIN(struct aica, aica_reg_map);
  // over allocate a bit to match the allocation granularity of the host
  AM_RANGE(0x00000000, 0x00010fff) AM_MOUNT("aica reg ram")
  AM_RANGE(0x00000000, 0x00010fff) AM_HANDLE("aica reg",
                                             (mmio_read_cb)&aica_reg_read,
                                             (mmio_write_cb)&aica_reg_write)
AM_END();

AM_BEGIN(struct aica, aica_data_map);
  AM_RANGE(0x00000000, 0x007fffff) AM_MOUNT("aica wave ram")
  AM_RANGE(0x00000000, 0x007fffff) AM_HANDLE("aica wave",
                                             (mmio_read_cb)&aica_wave_read,
                                             (mmio_write_cb)&aica_wave_write)
AM_END();
// clang-format on
