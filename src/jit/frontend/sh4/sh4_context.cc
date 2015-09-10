#include "jit/frontend/sh4/sh4_context.h"

namespace dreavm {
namespace jit {
namespace frontend {
namespace sh4 {

static void SetRegisterBank(SH4Context *ctx, int bank) {
  if (bank == 0) {
    for (int s = 0; s < 8; s++) {
      ctx->rbnk[1][s] = ctx->r[s];
      ctx->r[s] = ctx->rbnk[0][s];
    }
  } else {
    for (int s = 0; s < 8; s++) {
      ctx->rbnk[0][s] = ctx->r[s];
      ctx->r[s] = ctx->rbnk[1][s];
    }
  }
}

static void SwapFPRegisters(SH4Context *ctx) {
  uint32_t z;

  for (int s = 0; s <= 15; s++) {
    z = ctx->fr[s];
    ctx->fr[s] = ctx->xf[s];
    ctx->xf[s] = z;
  }
}

static void SwapFPCouples(SH4Context *ctx) {
  uint32_t z;

  for (int s = 0; s <= 15; s = s + 2) {
    z = ctx->fr[s];
    ctx->fr[s] = ctx->fr[s + 1];
    ctx->fr[s + 1] = z;

    z = ctx->xf[s];
    ctx->xf[s] = ctx->xf[s + 1];
    ctx->xf[s + 1] = z;
  }
}

void SRUpdated(SH4Context *ctx) {
  if (ctx->sr.RB != ctx->old_sr.RB) {
    SetRegisterBank(ctx, ctx->sr.RB ? 1 : 0);
  }

  ctx->old_sr = ctx->sr;
}

void FPSCRUpdated(SH4Context *ctx) {
  if (ctx->fpscr.FR != ctx->old_fpscr.FR) {
    SwapFPRegisters(ctx);
  }

  if (ctx->fpscr.PR != ctx->old_fpscr.PR) {
    SwapFPCouples(ctx);
  }

  ctx->old_fpscr = ctx->fpscr;
}
}
}
}
}
