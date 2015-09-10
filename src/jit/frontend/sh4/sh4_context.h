#ifndef SH4_CONTEXT_H
#define SH4_CONTEXT_H

#include <stdint.h>

namespace dreavm {
namespace jit {
namespace frontend {
namespace sh4 {

union SR_T {
  uint32_t full;
  struct {
    uint32_t T : 1;
    uint32_t S : 1;
    uint32_t reserved : 2;
    uint32_t IMASK : 4;
    uint32_t Q : 1;
    uint32_t M : 1;
    uint32_t reserved1 : 5;
    uint32_t FD : 1;
    uint32_t reserved2 : 12;
    uint32_t BL : 1;
    uint32_t RB : 1;
    uint32_t MD : 1;
    uint32_t reserved3 : 1;
  };
};

union FPSCR_T {
  uint32_t full;
  struct {
    uint32_t RM : 2;
    uint32_t flag : 5;
    uint32_t enable : 5;
    uint32_t cause : 6;
    uint32_t DN : 1;
    uint32_t PR : 1;
    uint32_t SZ : 1;
    uint32_t FR : 1;
    uint32_t reserved : 10;
  };
};

struct SH4Context {
  uint32_t pc, spc;
  uint32_t pr;
  uint32_t gbr, vbr;
  uint32_t mach, macl;
  uint32_t r[16], rbnk[2][8], sgr;
  uint32_t fr[16], xf[16];
  uint32_t fpul;
  uint32_t dbr;
  uint32_t sq[2][8];
  uint32_t sq_ext_addr[2];
  uint32_t preserve;
  SR_T sr, ssr, old_sr;
  FPSCR_T fpscr, old_fpscr;
};

void SRUpdated(SH4Context *ctx);
void FPSCRUpdated(SH4Context *ctx);
}
}
}
}

#endif
