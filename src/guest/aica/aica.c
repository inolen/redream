#include "guest/aica/aica.h"
#include "core/core.h"
#include "core/filesystem.h"
#include "guest/aica/aica_types.h"
#include "guest/arm7/arm7.h"
#include "guest/dreamcast.h"
#include "guest/holly/holly.h"
#include "guest/memory.h"
#include "guest/scheduler.h"
#include "guest/sh4/sh4.h"
#include "imgui.h"
#include "stats.h"

#if 0
#define LOG_AICA LOG_INFO
#else
#define LOG_AICA(...)
#endif

#define AICA_NUM_CHANNELS 64
#define AICA_BATCH_SIZE 10
#define AICA_TIMER_PERIOD 0xff

/* register access is performed with either 1 or 4 byte memory accesses. the
   physical registers however are only 2 bytes wide, with each one packing
   multiple values inside of it. align the offset to a 4 byte address and
   use lo / hi bools to simplify the logic around figuring out which values
   are being accessed */
#define AICA_REG_ALIGN(addr, mask) ((addr) & ~0x3)
#define AICA_REG_LO(addr, mask) (((addr)&0x3) == 0)
#define AICA_REG_HI(addr, mask) ((((addr)&0x3) != 0) || ((mask) != 0xff))

/* phase increment has 18 fractional bits */
#define AICA_PHASE_FRAC_BITS 18
#define AICA_PHASE_BASE (1 << AICA_PHASE_FRAC_BITS)

/* ADPCM decoding constants */
#define ADPCM_QUANT_MIN 0x7f
#define ADPCM_QUANT_MAX 0x6000

/* work with samples as 64-bit ints to avoid dealing with overflow issues
   during intermediate steps */
typedef int64_t sample_t;

/* amplitude / frequency envelope generator state */
struct aica_eg_state {
  int state;
  int attack_rate;
  int decay1_rate;
  int decay2_rate;
  int release_rate;
  int decay_level;
};

struct aica_channel {
  struct channel_data *data;

  int id;
  int active;

  /* base address in host memory of sound data */
  uint8_t *base;

  /* current position in the sound source */
  uint32_t phase;
  /* fractional remainder after phase increment */
  uint32_t phasefrc;
  /* amount to step the sound source each sample */
  uint32_t phaseinc;

  /* decoding state */
  sample_t prev_sample, prev_quant;
  sample_t next_sample, next_quant;
  sample_t loop_sample, loop_quant;

  /* signals the the current channel has looped */
  int looped;

  /*struct aica_eg_state aeg;
  struct aica_eg_state feg;*/
};

struct aica {
  struct device;
  uint8_t *aram;

  uint8_t reg[0x11000];

  /* reset state */
  int arm_resetting;

  /* timers */
  struct timer *timers[3];

  /* real-time clock */
  struct timer *rtc_timer;
  int rtc_write;
  uint32_t rtc;

  /* there are 64 channels, each with 32 x 16-bit registers arranged on 32-bit
     boundaries. the arm7 will perform either 32-bit or 8-bit accesses to the
     registers, while the sh4 will only perform 32-bit accesses as they must
     go through the g2 bus's fifo buffer */
  struct aica_channel channels[AICA_NUM_CHANNELS];
  struct common_data *common_data;
  struct timer *sample_timer;

  /* debugging */
  FILE *recording;
  int stream_stats;
};

/* approximated lookup tables for MVOL / TL scaling */
static sample_t mvol_scale[16];
static sample_t tl_scale[256];

static char *aica_fmt_names[] = {
    "PCMS16",       /* AICA_FMT_PCMS16 */
    "PCMS8",        /* AICA_FMT_PCMS8 */
    "ADPCM",        /* AICA_FMT_ADPCM */
    "ADPCM_STREAM", /* AICA_FMT_ADPCM_STREAM */
};

static char *aica_loop_names[] = {
    "LOOP_NONE",    /* AICA_LOOP_NONE */
    "LOOP_FORWARD", /* AICA_LOOP_FORWARD */
};

static void aica_init_tables() {
  static int initialized = 0;

  if (initialized) {
    return;
  }

  initialized = 1;

  /* the MVOL register adjusts the output level based on the table:

     MVOL       ∆level
     ----------------
     0         -MAX db
     1         -42 db
     2         -39 db
     ...
     n         -42 + (n-1) db

     sound pressure level is defined as:
     ∆level = 20 * log10(out / in)

     out can therefor be calculated as:
     out = in * pow(10, ∆level / 20)

     this can be approximated using MVOL instead of ∆level as:
     out = in / pow(2, (MVOL - i) / 2) */

  for (int i = 0; i < 16; i++) {
    /* 0 is a special case that mutes the output */
    if (i == 0) {
      continue;
    }

    /* a 32-bit int is used for the scale, leaving 15 bits for the fraction */
    mvol_scale[i] = (sample_t)((1 << 15) / pow(2.0f, (15 - i) / 2.0f));
  }

  /* each channel's TL register adjusts the output level based on the table:

     TL          ∆level
     --------------
     bit 0      -0.4 db
     bit 1      -0.8 db
     bit 2      -1.5 db
     bit 3      -3.0 db
     bit 4      -6.0 db
     bit 5      -12.0 db
     bit 6      -24.0 db
     bit 7      -48.0 db

     this can be approximated using TL as:
     out = in / pow(2, TL / 16) */

  for (int i = 0; i < 256; i++) {
    /* a 32-bit int is used for the scale, leaving 15 bits for the fraction */
    tl_scale[i] = (sample_t)((1 << 15) / pow(2.0f, i / 16.0f));
  }
}

static inline sample_t aica_adjust_master_volume(struct aica *aica,
                                                 sample_t in) {
  sample_t y = mvol_scale[aica->common_data->MVOL];
  /* truncate fraction */
  return (in * y) >> 15;
}

static inline sample_t aica_adjust_channel_volume(struct aica_channel *ch,
                                                  sample_t in) {
  sample_t y = tl_scale[ch->data->TL];
  /* truncate fraction */
  return (in * y) >> 15;
}

static void aica_decode_adpcm(uint8_t data, sample_t prev, sample_t prev_quant,
                              sample_t *next, sample_t *next_quant) {
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
  static sample_t adpcm_scale[8] = {1, 3, 5, 7, 9, 11, 13, 15};

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
  static sample_t adpcm_rate[8] = {230, 230, 230, 230, 307, 409, 512, 614};

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
  struct arm7 *arm7 = aica->dc->arm7;
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
    arm7_raise_interrupt(arm7, ARM7_INT_FIQ);
  }
}

static void aica_update_sh(struct aica *aica) {
  struct holly *hl = aica->dc->holly;
  uint32_t enabled_intr = aica->common_data->MCIEB;
  uint32_t pending_intr = aica->common_data->MCIPD & enabled_intr;

  if (pending_intr) {
    holly_raise_interrupt(hl, HOLLY_INT_G2AICINT);
  } else {
    holly_clear_interrupt(hl, HOLLY_INT_G2AICINT);
  }
}

static void aica_timer_reschedule(struct aica *aica, int n, uint32_t period);

static void aica_timer_expire(struct aica *aica, int n) {
  /* reschedule timer as soon as it expires */
  aica->timers[n] = NULL;
  aica_timer_reschedule(aica, n, AICA_TIMER_PERIOD);

  /*LOG_AICA("aica_timer_expire [%d]", n);*/

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
  struct scheduler *sched = aica->dc->sched;
  struct timer *timer = aica->timers[n];
  if (!timer) {
    /* if no timer has been created, return the raw value */
    return n == 0 ? aica->common_data->TIMA
                  : n == 1 ? aica->common_data->TIMB : aica->common_data->TIMC;
  }

  /* else, dynamically compute the value based on the timer's remaining time */
  int tctl = aica_timer_tctl(aica, n);
  int64_t freq = AICA_SAMPLE_FREQ >> tctl;
  int64_t remaining = sched_remaining_time(sched, timer);
  int64_t cycles = NANO_TO_CYCLES(remaining, freq);
  return (uint32_t)cycles;
}

static void aica_timer_reschedule(struct aica *aica, int n, uint32_t period) {
  struct scheduler *sched = aica->dc->sched;
  struct timer **timer = &aica->timers[n];

  int64_t freq = AICA_SAMPLE_FREQ >> aica_timer_tctl(aica, n);
  int64_t cycles = (int64_t)period;
  int64_t remaining = CYCLES_TO_NANO(cycles, freq);

  if (*timer) {
    sched_cancel_timer(sched, *timer);
    *timer = NULL;
  }

  static timer_cb timer_cbs[3] = {&aica_timer_expire_0, &aica_timer_expire_1,
                                  &aica_timer_expire_2};
  *timer = sched_start_timer(sched, timer_cbs[n], aica, remaining);
}

static uint32_t aica_rtc_reg_read(struct aica *aica, uint32_t addr,
                                  uint32_t mask) {
  switch (addr) {
    case 0x0:
      return aica->rtc >> 16;
    case 0x4:
      return aica->rtc & 0xffff;
    case 0x8:
      return 0;
    default:
      LOG_FATAL("aica_rtc_reg_read unexpected address 0x%x", addr);
      return 0;
  }
}

static void aica_rtc_reg_write(struct aica *aica, uint32_t addr, uint32_t data,
                               uint32_t mask) {
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
      LOG_FATAL("aica_rtc_reg_write unexpected address 0x%x", addr);
      break;
  }
}

static void aica_rtc_timer(void *data) {
  struct aica *aica = data;
  struct scheduler *sched = aica->dc->sched;
  aica->rtc++;
  aica->rtc_timer = sched_start_timer(sched, &aica_rtc_timer, aica, NS_PER_SEC);
}

static float aica_channel_hz(struct aica_channel *ch) {
  return (AICA_SAMPLE_FREQ * (float)ch->phaseinc) / AICA_PHASE_BASE;
}

static float aica_channel_duration(struct aica_channel *ch) {
  float hz = aica_channel_hz(ch);
  return ch->data->LEA / hz;
}

static uint32_t aica_channel_phaseinc(struct aica_channel *ch) {
  /* by default, increment by one sample per step */
  uint32_t phaseinc = AICA_PHASE_BASE;

  /* FNS represents the fractional phase increment, used to linearly interpolate
     between samples. note, the phase increment has 18 total fractional bits,
     but FNS is only 10 bits enabling lowest octave (which causes a right shift
     by 8) to still have 10 bits for interpolation */
  phaseinc |= ch->data->FNS << 8;

  /* OCT represents a full octave pitch shift in two's complement, ranging from
     -8 to +7 */
  uint32_t oct = ch->data->OCT;
  if (oct & 0x8) {
    phaseinc >>= (16 - oct);
  } else {
    phaseinc <<= oct;
  }

  return phaseinc;
}

static uint8_t *aica_channel_base(struct aica *aica, struct aica_channel *ch) {
  uint32_t start_addr = (ch->data->SA_hi << 16) | ch->data->SA_lo;
  return &aica->aram[start_addr];
}

static void aica_channel_key_off(struct aica *aica, struct aica_channel *ch) {
  if (!ch->active) {
    return;
  }

  ch->active = 0;

  /* this will already be cleared if the channel is stopped due to a key event.
     however, it will not be set when a non-looping channel is stopped */
  ch->data->KYONB = 0;

  LOG_AICA("aica_channel_key_off [%d]", ch->id);
}

static void aica_channel_key_on(struct aica *aica, struct aica_channel *ch) {
  if (ch->active) {
    return;
  }

  ch->active = 1;
  ch->base = aica_channel_base(aica, ch);
  ch->phase = 0;
  ch->phasefrc = 0;
  ch->phaseinc = aica_channel_phaseinc(ch);
  ch->looped = 0;
  ch->prev_sample = 0;
  ch->prev_quant = ADPCM_QUANT_MIN;
  ch->next_sample = 0;
  ch->next_quant = ADPCM_QUANT_MIN;
  ch->loop_sample = 0;
  ch->loop_quant = ADPCM_QUANT_MIN;

  LOG_AICA("aica_channel_key_on [%d] %s, %s, %.2f hz, %.2f sec", ch->id,
           aica_fmt_names[ch->data->PCMS], aica_loop_names[ch->data->LPCTL],
           aica_channel_hz(ch), aica_channel_duration(ch));
}

static void aica_channel_key_on_execute(struct aica *aica,
                                        struct aica_channel *ch) {
  if (!ch->data->KYONEX) {
    return;
  }

  /* modifying KYONEX for any channel will update the key state for all */
  for (int i = 0; i < AICA_NUM_CHANNELS; i++) {
    struct aica_channel *ch2 = &aica->channels[i];

    if (ch2->data->KYONB) {
      aica_channel_key_on(aica, ch2);
    } else {
      aica_channel_key_off(aica, ch2);
    }
  }

  /* register is read only */
  ch->data->KYONEX = 0;
}

static void aica_channel_step_one(struct aica *aica, struct aica_channel *ch) {
  CHECK_GE(ch->phasefrc, AICA_PHASE_BASE);

  /* decode the current sample */
  if (ch->data->SSCTL) {
    LOG_WARNING("SSCTL input not supported");
  } else {
    switch (ch->data->PCMS) {
      case AICA_FMT_PCMS16: {
        ch->next_sample = *(int16_t *)&ch->base[ch->phase << 1];
      } break;

      case AICA_FMT_PCMS8: {
        ch->next_sample = *(int8_t *)&ch->base[ch->phase] << 8;
      } break;

      case AICA_FMT_ADPCM:
      case AICA_FMT_ADPCM_STREAM: {
        int shift = (ch->phase & 1) << 2;
        uint8_t data = (ch->base[ch->phase >> 1] >> shift) & 0xf;
        aica_decode_adpcm(data, ch->prev_sample, ch->prev_quant,
                          &ch->next_sample, &ch->next_quant);
      } break;

      default:
        LOG_WARNING("unsupported PCMS %d", ch->data->PCMS);
        break;
    }
  }

  /* preserve decoding state previous to LSA for loops */
  if (ch->phase == ch->data->LSA) {
    ch->loop_sample = ch->prev_sample;
    ch->loop_quant = ch->prev_quant;
  }

  /* advance phase */
  ch->prev_sample = ch->next_sample;
  ch->prev_quant = ch->next_quant;
  ch->phasefrc -= AICA_PHASE_BASE;
  ch->phase++;

  /* check if the channel has looped */
  if (ch->phase >= ch->data->LEA) {
    ch->looped = 1;

    LOG_AICA("aica_channel_step [%d] looped", ch->id);

    switch (ch->data->LPCTL) {
      case AICA_LOOP_NONE: {
        aica_channel_key_off(aica, ch);
      } break;

      case AICA_LOOP_FORWARD: {
        /* restart channel */
        ch->phase = ch->data->LSA;

        /* in ADPCM streaming mode, the loop is a ring buffer. don't reset the
           decoding state in this case

           FIXME i'm not entirely sure this is accurate */
        if (ch->data->PCMS != AICA_FMT_ADPCM_STREAM) {
          ch->prev_sample = ch->loop_sample;
          ch->prev_quant = ch->loop_quant;
        }
      } break;
    }
  }
}

static sample_t aica_channel_step(struct aica *aica, struct aica_channel *ch) {
  if (!ch->active) {
    return 0;
  }

  CHECK_NOTNULL(ch->base);

  /* interpolate sample

     FIXME is this correct for the first sample */
  sample_t result = ch->prev_sample * (AICA_PHASE_BASE - ch->phasefrc);
  result += ch->next_sample * ch->phasefrc;
  result >>= AICA_PHASE_FRAC_BITS;

  /* advance the stream one sample at a time */
  ch->phasefrc += ch->phaseinc;

  while (ch->phasefrc >= AICA_PHASE_BASE) {
    aica_channel_step_one(aica, ch);
  }

  return result;
}

static void aica_generate_frames(struct aica *aica) {
  struct dreamcast *dc = aica->dc;
  int16_t buffer[AICA_BATCH_SIZE * 2];

  for (int frame = 0; frame < AICA_BATCH_SIZE; frame++) {
    sample_t l = 0;
    sample_t r = 0;

    for (int i = 0; i < AICA_NUM_CHANNELS; i++) {
      struct aica_channel *ch = &aica->channels[i];
      sample_t s = aica_channel_step(aica, ch);
      l += aica_adjust_channel_volume(ch, s);
      r += aica_adjust_channel_volume(ch, s);
    }

    l = aica_adjust_master_volume(aica, l);
    r = aica_adjust_master_volume(aica, r);

    buffer[frame * 2 + 0] = (int16_t)CLAMP(l, INT16_MIN, INT16_MAX);
    buffer[frame * 2 + 1] = (int16_t)CLAMP(r, INT16_MIN, INT16_MAX);
  }

  dc_push_audio(dc, buffer, AICA_BATCH_SIZE);

  /* save raw audio out while recording */
  if (aica->recording) {
    fwrite(buffer, 4, AICA_BATCH_SIZE, aica->recording);
  }

  prof_counter_add(COUNTER_aica_samples, AICA_BATCH_SIZE);
}

static uint32_t aica_channel_reg_read(struct aica *aica, uint32_t addr,
                                      uint32_t mask) {
  int n = addr >> 7;
  int offset = addr & ((1 << 7) - 1);
  struct aica_channel *ch = &aica->channels[n];

  /*LOG_AICA("aica_channel_reg_read [%d] 0x%x", ch->id, offset);*/

  return READ_DATA((uint8_t *)ch->data + offset);
}

static void aica_channel_reg_write(struct aica *aica, uint32_t addr,
                                   uint32_t data, uint32_t mask) {
  int n = addr >> 7;
  int offset = addr & ((1 << 7) - 1);
  struct aica_channel *ch = &aica->channels[n];

  /*LOG_AICA("aica_channel_reg_write [%d] 0x%x : 0x%x", ch->id, offset, data);*/
  WRITE_DATA((uint8_t *)ch->data + offset);

  int aligned = AICA_REG_ALIGN(offset, mask);
  int lo = AICA_REG_LO(offset, mask);
  int hi = AICA_REG_HI(offset, mask);

  switch (aligned) {
    case 0x0: { /* SA_hi, KYONB, KYONEX */
      if (lo) {
        ch->base = aica_channel_base(aica, ch);
      }
      if (hi) {
        aica_channel_key_on_execute(aica, ch);
      }
    } break;

    case 0x4: { /* SA_lo */
      ch->base = aica_channel_base(aica, ch);
    } break;

    case 0x18: { /* FNS, OCT */
      ch->phaseinc = aica_channel_phaseinc(ch);
    } break;
  }
}

static uint32_t aica_common_reg_read(struct aica *aica, uint32_t addr,
                                     uint32_t mask) {
  int aligned = AICA_REG_ALIGN(addr, mask);
  int lo = AICA_REG_LO(addr, mask);
  int hi = AICA_REG_HI(addr, mask);

  switch (aligned) {
    case 0x10: { /* EG, SGC, LP */
      /* reads the current EG / SGC / LP params from the stream specified by
         MSLC */
      struct aica_channel *ch = &aica->channels[aica->common_data->MSLC];

      /* EG straddles lo and hi bytes */
      if (lo || hi) {
        if (aica->common_data->AFSEL) {
          /* read FEG status */
        } else {
          /* read AEG status */
        }
      }

      if (hi) {
        aica->common_data->LP = ch->looped;
        ch->looped = 0;
      }
    } break;

    case 0x14: { /* CA */
      struct aica_channel *ch = &aica->channels[aica->common_data->MSLC];
      aica->common_data->CA = ch->phase;
    } break;

    case 0x90: { /* TIMA, TACTL */
      if (lo) {
        aica->common_data->TIMA =
            (aica_timer_tctl(aica, 0) << 8) | aica_timer_tcnt(aica, 0);
      }
    } break;

    case 0x94: { /* TIMB, TBCTL */
      if (lo) {
        aica->common_data->TIMB =
            (aica_timer_tctl(aica, 1) << 8) | aica_timer_tcnt(aica, 1);
      }
    } break;

    case 0x98: { /* TIMC, TCCTL */
      if (lo) {
        aica->common_data->TIMC =
            (aica_timer_tctl(aica, 2) << 8) | aica_timer_tcnt(aica, 2);
      }
    } break;
  }

  return READ_DATA((uint8_t *)aica->common_data + addr);
}

static void aica_common_reg_write(struct aica *aica, uint32_t addr,
                                  uint32_t data, uint32_t mask) {
  struct arm7 *arm7 = aica->dc->arm7;
  uint32_t old_data = READ_DATA((uint8_t *)aica->common_data + addr);
  WRITE_DATA((uint8_t *)aica->common_data + addr);

  int aligned = AICA_REG_ALIGN(addr, mask);
  int lo = AICA_REG_LO(addr, mask);
  int hi = AICA_REG_HI(addr, mask);

  switch (aligned) {
    case 0x90: { /* TIMA, TACTL */
      aica_timer_reschedule(aica, 0, AICA_TIMER_PERIOD - data);
    } break;

    case 0x94: { /* TIMB, TBCTL */
      aica_timer_reschedule(aica, 1, AICA_TIMER_PERIOD - data);
    } break;

    case 0x98: { /* TIMC, TCCTL */
      aica_timer_reschedule(aica, 2, AICA_TIMER_PERIOD - data);
    } break;

    case 0x9c: { /* SCIEB */
      aica_update_arm(aica);
    } break;

    case 0xa0: { /* SCIPD */
      /* only AICA_INT_DATA can be written to */
      CHECK(lo && hi);
      aica->common_data->SCIPD = old_data | (data & (1 << AICA_INT_DATA));
      aica_update_arm(aica);
    } break;

    case 0xa4: { /* SCIRE */
      aica->common_data->SCIPD &= ~aica->common_data->SCIRE;
      aica_update_arm(aica);
    } break;

    case 0xb4: { /* MCIEB */
      aica_update_sh(aica);
    } break;

    case 0xb8: { /* MCIPD */
      /* only AICA_INT_DATA can be written to */
      CHECK(lo && hi);
      aica->common_data->MCIPD = old_data | (data & (1 << AICA_INT_DATA));
      aica_update_sh(aica);
    } break;

    case 0xbc: { /* MCIRE */
      aica->common_data->MCIPD &= ~aica->common_data->MCIRE;
      aica_update_sh(aica);
    } break;

    case 0x400: { /* ARMRST, VREG */
      if (lo) {
        if (aica->common_data->ARMRST) {
          /* suspend arm when reset is pulled low */
          aica->arm_resetting = 1;
          arm7_suspend(arm7);
        } else if (aica->arm_resetting) {
          /* reset and resume arm when reset is released */
          aica->arm_resetting = 0;
          arm7_reset(arm7);
        }
      }
    } break;

    case 0x500: { /* L0-9 */
      LOG_FATAL("L0-9 assumed to be read-only");
    } break;

    case 0x504: { /* M0-9, RP */
      if (lo) {
        /* M is written to signal that the interrupt previously raised has
           finished processing */
        aica->common_data->L = 0;
        aica_update_arm(aica);
      }
    } break;
  }
}

static void aica_next_sample(void *data) {
  struct aica *aica = data;
  struct scheduler *sched = aica->dc->sched;

  aica_generate_frames(aica);
  aica_raise_interrupt(aica, AICA_INT_SAMPLE);
  aica_update_arm(aica);
  aica_update_sh(aica);

  /* reschedule */
  aica->sample_timer =
      sched_start_timer(sched, &aica_next_sample, aica,
                        HZ_TO_NANO(AICA_SAMPLE_FREQ / AICA_BATCH_SIZE));
}

static void aica_toggle_recording(struct aica *aica) {
  if (!aica->recording) {
    char filename[PATH_MAX];
    snprintf(filename, sizeof(filename), "%s" PATH_SEPARATOR "aica.pcm",
             fs_appdir());

    aica->recording = fopen(filename, "w");
    CHECK_NOTNULL(aica->recording, "Failed to open %s", filename);

    LOG_INFO("started recording audio to %s", filename);
  } else {
    fclose(aica->recording);
    aica->recording = NULL;

    LOG_INFO("stopped recording audio");
  }
}

static int aica_init(struct device *dev) {
  struct aica *aica = (struct aica *)dev;
  struct memory *mem = aica->dc->mem;
  struct scheduler *sched = aica->dc->sched;

  aica->aram = mem_aram(mem, 0x0);

  /* init channels */
  {
    for (int i = 0; i < AICA_NUM_CHANNELS; i++) {
      struct aica_channel *ch = &aica->channels[i];
      ch->data =
          (struct channel_data *)(aica->reg + sizeof(struct channel_data) * i);
    }
    aica->common_data = (struct common_data *)(aica->reg + 0x2800);
    aica->sample_timer =
        sched_start_timer(sched, &aica_next_sample, aica,
                          HZ_TO_NANO(AICA_SAMPLE_FREQ / AICA_BATCH_SIZE));
  }

  /* init timers */
  {
    for (int i = 0; i < 3; i++) {
      aica_timer_reschedule(aica, i, AICA_TIMER_PERIOD);
    }
  }

  /* init rtc */
  {
    /* increment clock every second */
    aica->rtc_timer =
        sched_start_timer(sched, &aica_rtc_timer, aica, NS_PER_SEC);
  }

  return 1;
}

void aica_reg_write(struct aica *aica, uint32_t addr, uint32_t data,
                    uint32_t mask) {
  if (addr < 0x2000) {
    aica_channel_reg_write(aica, addr, data, mask);
    return;
  } else if (addr >= 0x2800 && addr < 0x2d08) {
    aica_common_reg_write(aica, addr - 0x2800, data, mask);
    return;
  } else if (addr >= 0x10000 && addr < 0x1000c) {
    aica_rtc_reg_write(aica, addr - 0x10000, data, mask);
    return;
  }

  WRITE_DATA(&aica->reg[addr]);
}

uint32_t aica_reg_read(struct aica *aica, uint32_t addr, uint32_t mask) {
  if (addr < 0x2000) {
    return aica_channel_reg_read(aica, addr, mask);
  } else if (addr >= 0x2800 && addr < 0x2d08) {
    return aica_common_reg_read(aica, addr - 0x2800, mask);
  } else if (addr >= 0x10000 && addr < 0x1000c) {
    return aica_rtc_reg_read(aica, addr - 0x10000, mask);
  }
  return READ_DATA(&aica->reg[addr]);
}

void aica_mem_write(struct aica *aica, uint32_t addr, uint32_t data,
                    uint32_t mask) {
  WRITE_DATA(&aica->aram[addr]);
}

uint32_t aica_mem_read(struct aica *aica, uint32_t addr, uint32_t mask) {
  return READ_DATA(&aica->aram[addr]);
}

void aica_set_clock(struct aica *aica, uint32_t time) {
  aica->rtc = time;
}

#ifdef HAVE_IMGUI

#define CHANNEL_COLUMN(...)                       \
  for (int i = 0; i < AICA_NUM_CHANNELS; i++) {   \
    struct aica_channel *ch = &aica->channels[i]; \
    if (!ch->active) {                            \
      continue;                                   \
    }                                             \
    igText(__VA_ARGS__);                          \
  }                                               \
  igNextColumn();

void aica_debug_menu(struct aica *aica) {
  if (igBeginMainMenuBar()) {
    if (igBeginMenu("AICA", 1)) {
      const char *recording_label =
          aica->recording ? "stop recording" : "start recording";

      if (igMenuItem(recording_label, NULL, aica->recording, 1)) {
        aica_toggle_recording(aica);
      }

      if (igMenuItem("stream stats", NULL, aica->stream_stats, 1)) {
        aica->stream_stats = !aica->stream_stats;
      }

      igEndMenu();
    }

    igEndMainMenuBar();
  }

  if (aica->stream_stats) {
    if (igBegin("stream stats", NULL, 0)) {
      igColumns(8, NULL, 0);

      CHANNEL_COLUMN("%d", ch->id);
      CHANNEL_COLUMN(aica_fmt_names[ch->data->PCMS]);
      CHANNEL_COLUMN(aica_loop_names[ch->data->LPCTL]);
      CHANNEL_COLUMN("%.2f hz", aica_channel_hz(ch));
      CHANNEL_COLUMN("%.2f secs", aica_channel_duration(ch));
      CHANNEL_COLUMN(ch->looped ? "looped" : "not looped");
      CHANNEL_COLUMN(ch->data->KYONEX ? "KYONEX" : "");
      CHANNEL_COLUMN(ch->data->KYONB ? "KYONB" : "");

      igColumns(1, NULL, 0);

      igEnd();
    }
  }
}
#endif

void aica_destroy(struct aica *aica) {
  struct scheduler *sched = aica->dc->sched;

  /* shutdown rtc */
  {
    if (aica->rtc_timer) {
      sched_cancel_timer(sched, aica->rtc_timer);
    }
  }

  /* shutdown timers */
  {
    for (int i = 0; i < 3; i++) {
      if (aica->timers[i]) {
        sched_cancel_timer(sched, aica->timers[i]);
      }
    }
  }

  /* shutdown channels */
  {
    if (aica->sample_timer) {
      sched_cancel_timer(sched, aica->sample_timer);
    }
  }

  dc_destroy_device((struct device *)aica);
}

struct aica *aica_create(struct dreamcast *dc) {
  aica_init_tables();

  struct aica *aica =
      dc_create_device(dc, sizeof(struct aica), "aica", &aica_init, NULL);

  /* assign ids */
  for (int i = 0; i < AICA_NUM_CHANNELS; i++) {
    struct aica_channel *ch = &aica->channels[i];
    ch->id = i;
  }

  return aica;
}
