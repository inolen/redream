#ifndef ARMV3_CONTEXT_H
#define ARMV3_CONTEXT_H

#include <stdint.h>

enum {
  MODE_USR = 0b10000,
  MODE_FIQ = 0b10001,
  MODE_IRQ = 0b10010,
  MODE_SVC = 0b10011,
  MODE_ABT = 0b10111,
  MODE_UND = 0b11011,
  MODE_SYS = 0b11111,
};

enum {
  /*
   * indices 0-15 represent the registers for the current mode. during each
   * mode switch, the old mode's banked registers are stored out, while the
   * new mode's banked registers are loaded in to the active set
   */
  CPSR = 16,

  R8_FIQ,
  R9_FIQ,
  R10_FIQ,
  R11_FIQ,
  R12_FIQ,
  R13_FIQ,
  R14_FIQ,

  R13_SVC,
  R14_SVC,

  R13_ABT,
  R14_ABT,

  R13_IRQ,
  R14_IRQ,

  R13_UND,
  R14_UND,

  SPSR_FIQ,
  SPSR_SVC,
  SPSR_ABT,
  SPSR_IRQ,
  SPSR_UND,

  /*
   * each mode has its own saved status register. the MRS and MSR ops directly
   * access these. instead of having additional logic to access the correct
   * one for the current mode, a virtual SPSR register is used to represent
   * the SPSR for the current mode, and swapped out during each mode switch
   */
  SPSR,

  NUM_ARMV3_REGS,
};

/* PSR bits */
#define F_BIT 6
#define I_BIT 7
#define V_BIT 28
#define C_BIT 29
#define Z_BIT 30
#define N_BIT 31

#define M_MASK 0x1f
#define F_MASK (1u << F_BIT)
#define I_MASK (1u << I_BIT)
#define V_MASK (1u << V_BIT)
#define C_MASK (1u << C_BIT)
#define Z_MASK (1u << Z_BIT)
#define N_MASK (1u << N_BIT)

#define F_SET(sr) (((sr)&F_MASK) == F_MASK)
#define I_SET(sr) (((sr)&I_MASK) == I_MASK)
#define V_SET(sr) (((sr)&V_MASK) == V_MASK)
#define C_SET(sr) (((sr)&C_MASK) == C_MASK)
#define Z_SET(sr) (((sr)&Z_MASK) == Z_MASK)
#define N_SET(sr) (((sr)&N_MASK) == N_MASK)

#define F_CLEAR(sr) (!F_SET(sr))
#define I_CLEAR(sr) (!I_SET(sr))
#define V_CLEAR(sr) (!V_SET(sr))
#define C_CLEAR(sr) (!C_SET(sr))
#define Z_CLEAR(sr) (!Z_SET(sr))
#define N_CLEAR(sr) (!N_SET(sr))

struct armv3_context {
  uint32_t r[NUM_ARMV3_REGS];

  /* points directly to the user bank r0-15 no matter the mode */
  uint32_t *rusr[16];

  uint64_t pending_interrupts;

  /* the main dispatch loop is ran until run_cycles is <= 0 */
  int32_t run_cycles;

  /* debug information */
  int32_t ran_instrs;
};

/* map mode to SPSR / register layout */
extern const int armv3_spsr_table[0x20];
extern const int armv3_reg_table[0x20][16];

#endif
