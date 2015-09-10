#include "sh4_test.h"
#include "jit/frontend/sh4/sh4_context.h"

using namespace dreavm;
using namespace dreavm::jit::frontend::sh4;

SH4CTXReg sh4ctx_reg[] = {
#define SH4CTX(name, member, type)                      \
  { #name, offsetof(SH4Context, member), sizeof(type) } \
  ,
#include "sh4_ctx.inc"
#undef SH4CTX
};
