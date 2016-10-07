#include "hw/aica/aica.h"
#include "core/log.h"
#include "hw/aica/aica_types.h"
#include "hw/arm/arm.h"
#include "hw/dreamcast.h"
#include "hw/holly/holly.h"
#include "hw/memory.h"
#include "hw/scheduler.h"
#include "hw/sh4/sh4.h"

#define AICA_CLOCK_FREQ INT64_C(22579200)
#define AICA_SAMPLE_FREQ INT64_C(44100)
#define AICA_TIMER_PERIOD 0xff

struct aica {
  struct device;
  uint8_t *aica_regs;
  uint8_t *wave_ram;
  struct common_data *common;
  struct timer *timers[3];
};

static void aica_raise_interrupt(struct aica *aica, int intr) {
  aica->common->SCIPD |= intr;
}

static void aica_clear_interrupt(struct aica *aica, int intr) {
  aica->common->SCIPD &= ~intr;
}

static void aica_update_arm(struct aica *aica) {}

static void aica_update_sh(struct aica *aica) {
  uint32_t enabled_intr = aica->common->MCIEB;
  uint32_t pending_intr = aica->common->MCIPD & enabled_intr;

  if (pending_intr) {
    holly_toggle_interrupt(aica->holly, HOLLY_INTC_G2AICINT);
  }
}

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
  return n == 0 ? aica->common->TACTL : n == 1 ? aica->common->TBCTL
                                               : aica->common->TCCTL;
}

static uint32_t aica_timer_tcnt(struct aica *aica, int n) {
  struct timer *timer = aica->timers[n];
  if (!timer) {
    // if no timer has been created, return the raw value
    return n == 0 ? aica->common->TIMA : n == 1 ? aica->common->TIMB
                                                : aica->common->TIMC;
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

#define define_reg_read(name, type)                                          \
  type aica_reg_##name(struct aica *aica, uint32_t addr) {                   \
    if (addr >= 2800 /* common */) {                                         \
      switch (addr) {                                                        \
        case 0x90: /* TIMA */                                                \
          return (aica_timer_tctl(aica, 0) << 8) | aica_timer_tcnt(aica, 0); \
        case 0x94: /* TIMB */                                                \
          return (aica_timer_tctl(aica, 1) << 8) | aica_timer_tcnt(aica, 1); \
        case 0x98: /* TIMC */                                                \
          return (aica_timer_tctl(aica, 2) << 8) | aica_timer_tcnt(aica, 2); \
      }                                                                      \
      return *(type *)&aica->aica_regs[0x2800 + addr];                       \
    }                                                                        \
    return *(type *)&aica->aica_regs[addr];                                  \
  }

define_reg_read(r8, uint8_t);
define_reg_read(r16, uint16_t);
define_reg_read(r32, uint32_t);

#define define_reg_write(name, type)                                           \
  void aica_reg_##name(struct aica *aica, uint32_t addr, type value) {         \
    *(type *)&aica->aica_regs[addr] = value;                                   \
    if (addr >= 2800 /* common */) {                                           \
      addr -= 2800;                                                            \
                                                                               \
      switch (addr) {                                                          \
        case 0x90: { /* TIMA */                                                \
          aica_timer_reschedule(aica, 0,                                       \
                                AICA_TIMER_PERIOD - aica_timer_tcnt(aica, 0)); \
        } break;                                                               \
        case 0x94: { /* TIMB */                                                \
          aica_timer_reschedule(aica, 1,                                       \
                                AICA_TIMER_PERIOD - aica_timer_tcnt(aica, 1)); \
        } break;                                                               \
        case 0x98: { /* TIMC */                                                \
          aica_timer_reschedule(aica, 2,                                       \
                                AICA_TIMER_PERIOD - aica_timer_tcnt(aica, 2)); \
        } break;                                                               \
        case 0x400: { /* ARMRST */                                             \
          if (value) {                                                         \
            arm_suspend(aica->arm);                                            \
          } else {                                                             \
            arm_resume(aica->arm);                                             \
          }                                                                    \
        } break;                                                               \
      }                                                                        \
    }                                                                          \
  }

define_reg_write(w8, uint8_t);
define_reg_write(w16, uint16_t);
define_reg_write(w32, uint32_t);

#define define_read_wave(name, type)                        \
  type aica_wave_##name(struct aica *aica, uint32_t addr) { \
    return *(type *)&aica->wave_ram[addr];                  \
  }

define_read_wave(r8, uint8_t);
define_read_wave(r16, uint16_t);
uint32_t aica_wave_r32(struct aica *aica, uint32_t addr) {
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

  return *(uint32_t *)&aica->wave_ram[addr];
}

#define define_write_wave(name, type)                                   \
  void aica_wave_##name(struct aica *aica, uint32_t addr, type value) { \
    *(type *)&aica->wave_ram[addr] = value;                             \
  }

define_write_wave(w8, uint8_t);
define_write_wave(w16, uint16_t);
define_write_wave(w32, uint32_t);

static void aica_run(struct device *dev, int64_t ns) {
  struct aica *aica = (struct aica *)dev;

  int64_t cycles = MAX(NANO_TO_CYCLES(ns, AICA_CLOCK_FREQ), INT64_C(1));

  while (cycles > 0) {
    cycles--;
  }

  aica_raise_interrupt(aica, AICA_INT_SAMPLE);

  aica_update_arm(aica);
  aica_update_sh(aica);
}

static bool aica_init(struct device *dev) {
  struct aica *aica = (struct aica *)dev;

  aica->aica_regs = memory_translate(aica->memory, "aica reg ram", 0x00000000);
  aica->wave_ram = memory_translate(aica->memory, "aica wave ram", 0x00000000);
  aica->common = (struct common_data *)(aica->aica_regs + 0x2800);

  for (int i = 0; i < 3; i++) {
    aica_timer_reschedule(aica, i, AICA_TIMER_PERIOD);
  }

  // arm cpu is initially suspended
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
  dc_destroy_execute_interface(aica->execute_if);
  dc_destroy_device((struct device *)aica);
}

// clang-format off
AM_BEGIN(struct aica, aica_reg_map);
  // over allocate a bit to match the allocation granularity of the host
  AM_RANGE(0x00000000, 0x00010fff) AM_MOUNT("aica reg ram")
  AM_RANGE(0x00000000, 0x00010fff) AM_HANDLE("aica reg",
                                             (r8_cb)&aica_reg_r8,
                                             (r16_cb)&aica_reg_r16,
                                             (r32_cb)&aica_reg_r32,
                                             NULL,
                                             (w8_cb)&aica_reg_w8,
                                             (w16_cb)&aica_reg_w16,
                                             (w32_cb)&aica_reg_w32,
                                             NULL)
AM_END();

AM_BEGIN(struct aica, aica_data_map);
  AM_RANGE(0x00000000, 0x007fffff) AM_MOUNT("aica wave ram")
  AM_RANGE(0x00000000, 0x007fffff) AM_HANDLE("aica wave",
                                             (r8_cb)&aica_wave_r8,
                                             (r16_cb)&aica_wave_r16,
                                             (r32_cb)&aica_wave_r32,
                                             NULL,
                                             (w8_cb)&aica_wave_w8,
                                             (w16_cb)&aica_wave_w16,
                                             (w32_cb)&aica_wave_w32,
                                             NULL)
AM_END();
// clang-format on
