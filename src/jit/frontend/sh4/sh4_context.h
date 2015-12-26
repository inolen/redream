#ifndef SH4_CONTEXT_H
#define SH4_CONTEXT_H

#include <stdint.h>

namespace dreavm {
namespace jit {
namespace frontend {
namespace sh4 {

// SR bits
enum {
  T = 0x00000001,   // true / false condition or carry/borrow bit
  S = 0x00000002,   // specifies a saturation operation for a MAC instruction
  I = 0x000000f0,   // interrupt mask level
  Q = 0x00000100,   // used by the DIV0S, DIV0U, and DIV1 instructions
  M = 0x00000200,   // used by the DIV0S, DIV0U, and DIV1 instructions
  FD = 0x00008000,  // an FPU instr causes a general FPU disable exception
  BL = 0x10000000,  // interrupt requests are masked
  RB = 0x20000000,  // general register bank specifier in privileged mode (set
                    // to 1 by a reset, exception, or interrupt)
  MD = 0x40000000   // processor mode (0 is user mode, 1 is privileged mode)
};

// FPSCR bits
enum {
  RM = 0x00000003,
  DN = 0x00040000,
  PR = 0x00080000,
  SZ = 0x00100000,
  FR = 0x00200000
};

struct SH4Context {
  // IRBuilder only supports 64-bit arguments for external calls atm
  void *sh4;
  void (*Pref)(SH4Context *, uint64_t addr);
  void (*SRUpdated)(SH4Context *, uint64_t old_sr);
  void (*FPSCRUpdated)(SH4Context *, uint64_t old_fpscr);

  int cycles;

  uint32_t pc, spc;
  uint32_t pr;
  uint32_t gbr, vbr;
  uint32_t mach, macl;
  uint32_t r[16], ralt[8], sgr;
  uint32_t fr[16], xf[16];
  uint32_t fpul;
  uint32_t dbr;
  uint32_t sq[2][8];
  uint32_t sr_qm;
  uint32_t sr, ssr;
  uint32_t fpscr;
};
}
}
}
}

#endif
