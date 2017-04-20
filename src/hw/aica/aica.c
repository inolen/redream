#include "hw/aica/aica.h"
#include "core/log.h"
#include "core/option.h"
#include "core/profiler.h"
#include "core/ringbuf.h"
#include "hw/aica/aica_types.h"
#include "hw/arm7/arm7.h"
#include "hw/dreamcast.h"
#include "hw/holly/holly.h"
#include "hw/memory.h"
#include "hw/scheduler.h"
#include "hw/sh4/sh4.h"
#include "sys/filesystem.h"
#include "ui/nuklear.h"

DEFINE_OPTION_INT(rtc, 0, OPTION_HIDDEN);
DEFINE_AGGREGATE_COUNTER(aica_samples);

#if 0
#define LOG_AICA LOG_INFO
#else
#define LOG_AICA(...)
#endif

#define AICA_SAMPLE_BATCH 10
#define AICA_NUM_CHANNELS 64

#define AICA_FNS_BITS 10
#define AICA_OFFSET_POS(s) (s >> AICA_FNS_BITS)
#define AICA_OFFSET_FRAC(s) (s & ((1 << AICA_FNS_BITS) - 1))

#define AICA_TIMER_PERIOD 0xff

#define ADPCM_QUANT_MIN 0x7f
#define ADPCM_QUANT_MAX 0x6000

struct aica_channel {
  struct channel_data *data;

  int active;

  /* signals the the current channel has looped */
  int looped;

  /* base address in host memory of sound data */
  uint8_t *base;

  /* how much to step the sound source each sample */
  uint32_t step;

  /* current position in the sound source */
  uint32_t offset;

  /* previous sample state */
  int32_t prev_sample;
  int32_t prev_quant;
};

struct aica {
  struct device;
  uint8_t reg[0x11000];
  uint8_t *wave_ram;

  /* reset state */
  int arm_resetting;

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
  struct ringbuf *frames;

  /* raw audio recording */
  FILE *recording;
};

static void aica_decode_adpcm(int32_t data, int32_t prev, int32_t prev_quant,
                              int32_t *next, int32_t *next_quant) {
  /* the decoded value (n) = (1 - 2 * l4) * (l3 + l2/2 + l1/4 + 1/8) * quantized
     width (n) + decoded value (n - 1)

     a lookup table is used to compute the second part of the above expression:

     l3  l2  l1  f
     --------------
     0   0   0   1
     0   0   1   3
     0   1   0   5
     0   1   1   7
     1   0   0   9
     1   0   1   11
     1   1   0   13
     1   1   1   15

     the final value is a signed 16-bit value and must be clamped as such */
  static int32_t adpcm_scale[8] = {1, 3, 5, 7, 9, 11, 13, 15};

  int l4 = data >> 3;
  int l321 = data & 0x7;
  int sign = 1 - 2 * l4;

  *next = sign * ((adpcm_scale[l321] * prev_quant) >> 3) + prev;
  *next = CLAMP(*next, INT16_MIN, INT16_MAX);

  /* the quantized width (n+1) = f(l3, l2, l1) * quantized width (n).
     f(l3, l2, l1) is the rate of change in the quantized width found
     from the table:

     l3  l2  l1  f
     ----------------------
     0   0   0   0.8984375   (230 / 256)
     0   0   1   0.8984375   (230 / 256)
     0   1   0   0.8984375   (230 / 256)
     0   1   1   0.8984375   (230 / 256)
     1   0   0   1.19921875  (307 / 256)
     1   0   1   1.59765625  (409 / 256)
     1   1   0   2.0         (512 / 256)
     1   1   1   2.3984375   (614 / 256)

     the quantized width's min value is 127, and its max value is 24576 */
  static int32_t adpcm_rate[8] = {230, 230, 230, 230, 307, 409, 512, 614};

  *next_quant = (prev_quant * adpcm_rate[l321]) >> 8;
  *next_quant = CLAMP(*next_quant, ADPCM_QUANT_MIN, ADPCM_QUANT_MAX);
}

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

  /* avoid reentering FIQ handler if it hasn't completed */
  if (aica->common_data->L) {
    return;
  }

  if (pending_intr) {
    for (uint32_t i = 0; i < NUM_AICA_INT; i++) {
      if (pending_intr & (1 << i)) {
        aica->common_data->L = aica_encode_arm_irq_l(aica, i);
        break;
      }
    }
  }

  if (aica->common_data->L) {
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
  return n == 0 ? aica->common_data->TACTL
                : n == 1 ? aica->common_data->TBCTL : aica->common_data->TCCTL;
}

static uint32_t aica_timer_tcnt(struct aica *aica, int n) {
  struct timer *timer = aica->timers[n];
  if (!timer) {
    /* if no timer has been created, return the raw value */
    return n == 0 ? aica->common_data->TIMA
                  : n == 1 ? aica->common_data->TIMB : aica->common_data->TIMC;
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
      LOG_FATAL("Unexpected rtc address");
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
      LOG_FATAL("Unexpected rtc address");
      break;
  }
}

static void aica_rtc_timer(void *data) {
  struct aica *aica = data;
  aica->rtc++;
  aica->rtc_timer =
      scheduler_start_timer(aica->scheduler, &aica_rtc_timer, aica, NS_PER_SEC);
}

static uint32_t aica_channel_step(struct aica_channel *ch) {
  /* by default, step the stream a single sample at a time */
  uint32_t step = 1 << AICA_FNS_BITS;

  /* FNS represents the fractional portion of a step, used to linearly
     interpolate between samples */
  step |= ch->data->FNS;

  /* OCT represents a full octave pitch shift in two's complement, ranging
     from -8 to +7 */
  uint32_t oct = ch->data->OCT;
  if (oct & 8) {
    step >>= (16 - oct);
  } else {
    step <<= oct;
  }

  return step;
}

static void aica_channel_stop(struct aica *aica, struct aica_channel *ch) {
  if (!ch->active) {
    return;
  }

  ch->active = 0;

  LOG_AICA("aica_channel_stop %d", ch - aica->channels);
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
  ch->prev_sample = 0;
  ch->prev_quant = ADPCM_QUANT_MIN;

  LOG_AICA("aica_channel_start %d", ch - aica->channels);
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

static int32_t aica_channel_update(struct aica *aica, struct aica_channel *ch) {
  if (!ch->active) {
    return 0;
  }

  CHECK_NOTNULL(ch->base);

  int pos = AICA_OFFSET_POS(ch->offset);
  int frac = AICA_OFFSET_FRAC(ch->offset);
  int32_t next_sample = 0;
  int32_t next_quant = 0;

  switch (ch->data->PCMS) {
    /* 16-bit signed PCM */
    case 0: {
      next_sample = *(int16_t *)&ch->base[pos << 1];
    } break;

    /* 8-bit signed PCM */
    case 1: {
      next_sample = *(int8_t *)&ch->base[pos] << 8;
    } break;

    /* 4-bit ADPCM */
    case 2:
    case 3: {
      int shift = (pos & 1) << 2;
      int32_t data = (ch->base[pos >> 1] >> shift) & 0xf;
      aica_decode_adpcm(data, ch->prev_sample, ch->prev_quant, &next_sample,
                        &next_quant);
    } break;

    default:
      LOG_WARNING("Unsupported PCMS %d", ch->data->PCMS);
      break;
  }

  /* interpolate sample */
  int32_t result = ch->prev_sample * ((1 << AICA_FNS_BITS) - frac);
  result += next_sample * frac;
  result >>= AICA_FNS_BITS;

  /* step forward */
  ch->offset += ch->step;
  ch->prev_sample = next_sample;
  ch->prev_quant = next_quant;

  /* check if the current position in the sound source has passed the loop end
     position */
  if (pos > ch->data->LEA) {
    if (ch->data->LPCTL) {
      /* restart channel at LSA */
      LOG_AICA("aica_channel_step %d restart", ch - aica->channels);
      ch->offset = ch->data->LSA << AICA_FNS_BITS;
      ch->prev_sample = 0;
      ch->prev_quant = ADPCM_QUANT_MIN;
      ch->looped = 1;
    } else {
      aica_channel_stop(aica, ch);
    }
  }

  return result;
}

static void aica_write_frames(struct aica *aica, const void *frames,
                              int num_frames) {
  int remaining = ringbuf_remaining(aica->frames);
  int size = MIN(remaining, num_frames * 4);
  CHECK_EQ(size % 4, 0);

  void *write_ptr = ringbuf_write_ptr(aica->frames);
  memcpy(write_ptr, frames, size);
  ringbuf_advance_write_ptr(aica->frames, size);

  /* save raw audio out while recording */
  if (aica->recording) {
    fwrite(frames, 1, size, aica->recording);
  }
}

int aica_read_frames(struct aica *aica, void *frames, int num_frames) {
  int available = ringbuf_available(aica->frames);
  int size = MIN(available, num_frames * 4);
  CHECK_EQ(size % 4, 0);

  void *read_ptr = ringbuf_read_ptr(aica->frames);
  memcpy(frames, read_ptr, size);
  ringbuf_advance_read_ptr(aica->frames, size);

  return size / 4;
}

int aica_available_frames(struct aica *aica) {
  int available = ringbuf_available(aica->frames);
  return available / 4;
}

static void aica_generate_frames(struct aica *aica, int num_frames) {
  int32_t frames[10];

  for (int frame = 0; frame < num_frames;) {
    int n = MIN(num_frames - frame, array_size(frames));

    for (int i = 0; i < n; i++) {
      int32_t l = 0;
      int32_t r = 0;

      for (int j = 0; j < AICA_NUM_CHANNELS; j++) {
        struct aica_channel *ch = &aica->channels[j];
        int32_t s = aica_channel_update(aica, ch);
        l += s;
        r += s;
      }

      l = CLAMP(l, INT16_MIN, INT16_MAX);
      r = CLAMP(r, INT16_MIN, INT16_MAX);

      frames[i] = (r << 16) | l;
    }

    aica_write_frames(aica, &frames, n);
    prof_counter_add(COUNTER_aica_samples, n);

    frame += n;
  }
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
      struct aica_channel *ch = &aica->channels[aica->common_data->MSLC];

      if ((addr == 0x10 && DATA_SIZE() == 2) ||
          (addr == 0x11 && DATA_SIZE() == 1)) {
        aica->common_data->LP = ch->looped;
        ch->looped = 0;
      }
    } break;
    case 0x14: { /* CA */
      struct aica_channel *ch = &aica->channels[aica->common_data->MSLC];
      aica->common_data->CA = AICA_OFFSET_POS(ch->offset);
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
  }

  return READ_DATA((uint8_t *)aica->common_data + addr);
}

static void aica_common_reg_write(struct aica *aica, uint32_t addr,
                                  uint32_t data, uint32_t data_mask) {
  uint32_t old_data = READ_DATA((uint8_t *)aica->common_data + addr);

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
    case 0x9d: { /* SCIEB */
      aica_update_arm(aica);
    } break;
    case 0xa0:
    case 0xa1: { /* SCIPD */
      /* only AICA_INT_DATA can be written to */
      CHECK(DATA_SIZE() >= 2 && addr == 0xa0);
      aica->common_data->SCIPD = old_data | (data & (1 << AICA_INT_DATA));
      aica_update_arm(aica);
    } break;
    case 0xa4:
    case 0xa5: { /* SCIRE */
      aica->common_data->SCIPD &= ~aica->common_data->SCIRE;
      aica_update_arm(aica);
    } break;
    case 0xb4:
    case 0xb5: { /* MCIEB */
      aica_update_sh(aica);
    } break;
    case 0xb8:
    case 0xb9: { /* MCIPD */
      /* only AICA_INT_DATA can be written to */
      CHECK(DATA_SIZE() >= 2 && addr == 0xb8);
      aica->common_data->MCIPD = old_data | (data & (1 << AICA_INT_DATA));
      aica_update_sh(aica);
    } break;
    case 0xbc:
    case 0xbd: { /* MCIRE */
      aica->common_data->MCIPD &= ~aica->common_data->MCIRE;
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
      LOG_FATAL("L0-9 assumed to be read-only");
    } break;
    case 0x504: { /* M0-9 */
      /* M is written to signal that the interrupt previously raised has
         finished processing */
      aica->common_data->L = 0;
      aica_update_arm(aica);
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

  aica_generate_frames(aica, AICA_SAMPLE_BATCH);
  aica_raise_interrupt(aica, AICA_INT_SAMPLE);
  aica_update_arm(aica);
  aica_update_sh(aica);

  /* reschedule */
  aica->sample_timer =
      scheduler_start_timer(aica->scheduler, &aica_next_sample, aica,
                            HZ_TO_NANO(AICA_SAMPLE_FREQ / AICA_SAMPLE_BATCH));
}

static void aica_toggle_recording(struct aica *aica) {
  if (!aica->recording) {
    char filename[PATH_MAX];
    snprintf(filename, sizeof(filename), "%s" PATH_SEPARATOR "aica.pcm",
             fs_appdir());

    aica->recording = fopen(filename, "w");
    CHECK_NOTNULL(aica->recording, "Failed to open %s", filename);
  } else {
    fclose(aica->recording);
    aica->recording = NULL;
  }
}

static void aica_debug_menu(struct device *dev, struct nk_context *ctx) {
  struct aica *aica = (struct aica *)dev;

  nk_layout_row_push(ctx, 40.0f);

  if (nk_menu_begin_label(ctx, "AICA", NK_TEXT_LEFT, nk_vec2(140.0f, 200.0f))) {
    nk_layout_row_dynamic(ctx, DEBUG_MENU_HEIGHT, 1);

    if (!aica->recording && nk_button_label(ctx, "stard recording")) {
      aica_toggle_recording(aica);
    } else if (aica->recording && nk_button_label(ctx, "stop recording")) {
      aica_toggle_recording(aica);
    }

    nk_menu_end(ctx);
  }
}

static int aica_init(struct device *dev) {
  struct aica *aica = (struct aica *)dev;

  aica->wave_ram = memory_translate(aica->memory, "aica wave ram", 0x00000000);

  /* init channels */
  {
    for (int i = 0; i < AICA_NUM_CHANNELS; i++) {
      struct aica_channel *ch = &aica->channels[i];
      ch->data =
          (struct channel_data *)(aica->reg + sizeof(struct channel_data) * i);
    }
    aica->common_data = (struct common_data *)(aica->reg + 0x2800);
    aica->sample_timer =
        scheduler_start_timer(aica->scheduler, &aica_next_sample, aica,
                              HZ_TO_NANO(AICA_SAMPLE_FREQ / AICA_SAMPLE_BATCH));
  }

  /* init timers */
  {
    for (int i = 0; i < 3; i++) {
      aica_timer_reschedule(aica, i, AICA_TIMER_PERIOD);
    }
  }

  /* init rtc */
  {
    /* seed clock from persistant options */
    CHECK(sizeof(aica->rtc) <= sizeof(OPTION_rtc));
    aica->rtc = *(uint32_t *)&OPTION_rtc;

    /* increment clock every second */
    aica->rtc_timer = scheduler_start_timer(aica->scheduler, &aica_rtc_timer,
                                            aica, NS_PER_SEC);
  }

  return 1;
}

void aica_destroy(struct aica *aica) {
  /* shutdown rtc */
  {
    if (aica->rtc_timer) {
      scheduler_cancel_timer(aica->scheduler, aica->rtc_timer);
    }

    /* persist clock */
    OPTION_rtc = *(int *)&aica->rtc;
  }

  /* shutdown timers */
  {
    for (int i = 0; i < 3; i++) {
      if (aica->timers[i]) {
        scheduler_cancel_timer(aica->scheduler, aica->timers[i]);
      }
    }
  }

  /* shutdown channels */
  {
    if (aica->sample_timer) {
      scheduler_cancel_timer(aica->scheduler, aica->sample_timer);
    }
  }

  ringbuf_destroy(aica->frames);
  dc_destroy_window_interface(aica->window_if);
  dc_destroy_device((struct device *)aica);
}

struct aica *aica_create(struct dreamcast *dc) {
  struct aica *aica =
      dc_create_device(dc, sizeof(struct aica), "aica", &aica_init);

  aica->window_if =
      dc_create_window_interface(&aica_debug_menu, NULL, NULL, NULL);
  aica->frames = ringbuf_create(AICA_SAMPLE_FREQ * 4);

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
