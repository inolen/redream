#ifndef SH4_CONTEXT_H
#define SH4_CONTEXT_H

#include <stdint.h>

/* SR bits */

/* true / false condition or carry/borrow bit */
#define T_BIT 0
/* specifies a saturation operation for a MAC instruction */
#define S_BIT 1
/* interrupt mask level */
#define I_BIT 4
/* used by the DIV0S, DIV0U, and DIV1 instructions */
#define Q_BIT 8
#define M_BIT 9
/* an FPU instr causes a general FPU disable exception */
#define FD_BIT 15
/* interrupt requests are masked */
#define BL_BIT 28
/* general register bank specifier in privileged mode (set
   to 1 by a reset, exception, or interrupt) */
#define RB_BIT 29
/* processor mode (0 is user mode, 1 is privileged mode) */
#define MD_BIT 30

#define T_MASK (1u << T_BIT)
#define S_MASK (1u << S_BIT)
#define I_MASK 0xf0
#define Q_MASK (1u << Q_BIT)
#define M_MASK (1u << M_BIT)
#define FD_MASK (1u << FD_BIT)
#define BL_MASK (1u << BL_BIT)
#define RB_MASK (1u << RB_BIT)
#define MD_MASK (1u << MD_BIT)

/* FPSCR bits */

/* denormalization mode */
#define DN_BIT 18
/* precision mode */
#define PR_BIT 19
/* transfer size mode */
#define SZ_BIT 20
/* floating-point register bank */
#define FR_BIT 21

#define RM_MASK 0x3
#define DN_MASK (1u << DN_BIT)
#define PR_MASK (1u << PR_BIT)
#define SZ_MASK (1u << SZ_BIT)
#define FR_MASK (1u << FR_BIT)

struct sh4_ctx {
  /* there are 24 32-bit general registers, r0_bank0-r7_bank0, r0_bank1-r7_bank1
     and r8-r15. r contains the active bank's r0-r7 as well as r8-r15. ralt
     contains the inactive bank's r0-r7 and is swapped in when the processor
     mode changes */
  uint32_t r[16], ralt[8];

  /* there are 32 32-bit floating point registers, fr0-fr15 and xf0-xf15. these
     registers are banked, and swapped with eachother when the bank bit of
     FPSCR changes. in addition, fr0–fr15 can be used as the eight registers
     dr0/2/4/6/8/10/12/14 (double-precision, or pair registers) or the four
     registers fv0/4/8/12 (vector registers). while xf0-xf15 can be used as
     the eight registers xd0/2/4/6/8/10/12/14 (pair registers) or register
     matrix XMTRX

     note, the sh4 does not support endian conversion for 64-bit data.
     therefore, if 64-bit floating point access is performed in little endian
     mode, the upper and lower 32 bits will be reversed. for example, dr2
     aliases fr2 and fr3, but fr3 is actually the low-order word

     in order to avoid swapping the words in every double-precision opcode, the
     mapping for each pair of single-precision registers is instead swapped by
     XOR'ing the actual index with 1. for example, fr2 becomes fr[3] and fr3
     becomes fr[2], enabling dr2 to perfectly alias fr[2]

     note note, this incorrectly causes fv registers to be swizzled. fv0 should
     be loaded as {fr0, fr1, fr2, fr3} but it's actually loaded as
     {fr1, fr0, fr3, fr2}. however, due to the way the FV registers are
     used (FIPR and FTRV) this doesn't actually affect the results */
  uint32_t fr[16], xf[16];

  uint32_t pc, pr, sr, sr_t, sr_s, sr_qm, fpscr;
  uint32_t dbr, gbr, vbr;
  uint32_t fpul, mach, macl;
  uint32_t sgr, spc, ssr;
  uint64_t pending_interrupts;
  uint32_t sq[2][8];

  /* the main dispatch loop is ran until remaining_cycles is <= 0 */
  int32_t remaining_cycles;

  /* debug information */
  int64_t ran_instrs;

  uint8_t cache[0x2000];
};

static inline void sh4_implode_sr(struct sh4_ctx *ctx) {
  ctx->sr &= ~(S_MASK | T_MASK);
  ctx->sr |= (ctx->sr_s << S_BIT) | (ctx->sr_t << T_BIT);
}

static inline void sh4_explode_sr(struct sh4_ctx *ctx) {
  ctx->sr_t = (ctx->sr & T_MASK) >> T_BIT;
  ctx->sr_s = (ctx->sr & S_MASK) >> S_BIT;
}

#endif
