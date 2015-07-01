#include "sh4_test.h"
#include "cpu/sh4.h"

using namespace dreavm;
using namespace dreavm::cpu;

SH4CTXReg sh4ctx_reg[] = {
#define SH4CTX(name, member, type)                      \
  { #name, offsetof(SH4Context, member), sizeof(type) } \
  ,
#include "sh4_ctx.inc"
#undef SH4CTX
};
