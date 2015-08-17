#include "cpu/frontend/sh4/sh4_emit.h"
#include "cpu/frontend/sh4/sh4_frontend.h"
#include "cpu/frontend/sh4/sh4_instr.h"
#include "cpu/runtime.h"
#include "emu/profiler.h"

using namespace dreavm::cpu;
using namespace dreavm::cpu::frontend;
using namespace dreavm::cpu::frontend::sh4;
using namespace dreavm::cpu::ir;
using namespace dreavm::emu;

SH4Builder::SH4Builder(Memory &memory)
    : memory_(memory),
      has_delay_instr_(false),
      preserve_offset_(-1),
      preserve_mask_(0),
      offset_preserved_(false) {}

SH4Builder::~SH4Builder() {}

void SH4Builder::Emit(uint32_t start_addr, const SH4Context &ctx) {
  PROFILER_RUNTIME("SH4Builder::Emit");

  uint32_t addr = start_addr;

  // use fpu state when generating code. we could emit branches that check this
  // state in the actual IR, but that's extremely slow
  fpu_state_.double_precision = ctx.fpscr.PR;
  fpu_state_.single_precision_pair = ctx.fpscr.SZ;

  while (true) {
    Instr instr(addr, memory_.R16(addr));
    bool delayed = instr.type->flags & OP_FLAG_DELAYED;

    guest_cycles_ += instr.type->cycles;

    // save off the delay instruction if we need to
    if (delayed) {
      delay_instr_ = Instr(addr + 2, memory_.R16(addr + 2));
      has_delay_instr_ = true;

      guest_cycles_ += delay_instr_.type->cycles;
    }

    // emit the current instruction
    (emit_callbacks[instr.type->op])(*this, fpu_state_, instr);

    // delayed instructions will be emitted already by the instructions handler
    addr += delayed ? 4 : 2;

    // if fpscr is changed, stop emitting since the fpu state is invalidated
    if (instr.type->flags & OP_FLAG_SET_FPSCR) {
      Branch(AllocConstant(addr));
      break;
    }

    // end block once a branch is hit
    if (instr.type->flags & OP_FLAG_BRANCH) {
      break;
    }
  }

  // if the final block is empty, emitting stopped on a conditional branch
  // probably. if that's the case, update it's destination to be an address
  // and remove this empty block
  // FIXME this feels pretty wrong
  auto it = blocks_.end();
  ir::Block *last_block = *(--it);
  ir::Block *second_to_last_block = *(--it);
  if (!last_block->instrs().head()) {
    ir::Instr *tail_instr = second_to_last_block->instrs().tail();
    CHECK_EQ(tail_instr->op(), OP_BRANCH_COND);

    if (tail_instr->arg1()->type() == VALUE_BLOCK &&
        tail_instr->arg1()->value<Block *>() == last_block) {
      tail_instr->set_arg1(AllocConstant(addr));
    } else if (tail_instr->arg2()->type() == VALUE_BLOCK &&
               tail_instr->arg2()->value<Block *>() == last_block) {
      tail_instr->set_arg2(AllocConstant(addr));
    }

    RemoveBlock(last_block);
  }
}

Value *SH4Builder::LoadRegister(int n, ValueTy type) {
  return LoadContext(offsetof(SH4Context, r[n]), type);
}

void SH4Builder::StoreRegister(int n, Value *v) {
  CHECK_EQ(v->type(), VALUE_I32);
  return StoreAndPreserveContext(offsetof(SH4Context, r[n]), v);
}

Value *SH4Builder::LoadRegisterF(int n, ValueTy type) {
  return LoadContext(offsetof(SH4Context, fr[n]), type);
}

void SH4Builder::StoreRegisterF(int n, Value *v) {
  return StoreAndPreserveContext(offsetof(SH4Context, fr[n]), v);
}

Value *SH4Builder::LoadRegisterXF(int n, ValueTy type) {
  return LoadContext(offsetof(SH4Context, xf[n]), type);
}

void SH4Builder::StoreRegisterXF(int n, Value *v) {
  return StoreAndPreserveContext(offsetof(SH4Context, xf[n]), v);
}

Value *SH4Builder::LoadSR() {
  return LoadContext(offsetof(SH4Context, sr), VALUE_I32);
}

void SH4Builder::StoreSR(Value *v) {
  CHECK_EQ(v->type(), VALUE_I32);
  StoreAndPreserveContext(offsetof(SH4Context, sr), v, IF_INVALIDATE_CONTEXT);
  CallExternal((ExternalFn)&SRUpdated);
}

ir::Value *SH4Builder::LoadT() { return And(LoadSR(), AllocConstant(T)); }

void SH4Builder::StoreT(ir::Value *v) {
  Value *sr = LoadSR();
  StoreSR(Select(v, Or(sr, AllocConstant(T)), And(sr, AllocConstant(~T))));
}

Value *SH4Builder::LoadGBR() {
  return LoadContext(offsetof(SH4Context, gbr), VALUE_I32);
}

void SH4Builder::StoreGBR(Value *v) {
  StoreAndPreserveContext(offsetof(SH4Context, gbr), v);
}

ir::Value *SH4Builder::LoadFPSCR() {
  ir::Value *v = LoadContext(offsetof(SH4Context, fpscr), VALUE_I32);
  v = And(v, AllocConstant(0x003fffff));
  return v;
}

void SH4Builder::StoreFPSCR(ir::Value *v) {
  CHECK_EQ(v->type(), VALUE_I32);
  v = And(v, AllocConstant(0x003fffff));
  StoreAndPreserveContext(offsetof(SH4Context, fpscr), v);
  CallExternal((ExternalFn)&FPSCRUpdated);
}

ir::Value *SH4Builder::LoadPR() {
  return LoadContext(offsetof(SH4Context, pr), VALUE_I32);
}

void SH4Builder::StorePR(ir::Value *v) {
  CHECK_EQ(v->type(), VALUE_I32);
  StoreAndPreserveContext(offsetof(SH4Context, pr), v);
}

void SH4Builder::PreserveT() {
  preserve_offset_ = offsetof(SH4Context, sr);
  preserve_mask_ = T;
}

void SH4Builder::PreservePR() {
  preserve_offset_ = offsetof(SH4Context, pr);
  preserve_mask_ = 0;
}

void SH4Builder::PreserveRegister(int n) {
  preserve_offset_ = offsetof(SH4Context, r[n]);
  preserve_mask_ = 0;
}

Value *SH4Builder::LoadPreserved() {
  Value *v = offset_preserved_
                 // if the offset had to be preserved, load it up
                 ? LoadContext(offsetof(SH4Context, preserve), VALUE_I32)
                 // else, load from its original location
                 : LoadContext(preserve_offset_, VALUE_I32);

  if (preserve_mask_) {
    v = And(v, AllocConstant(preserve_mask_));
  }

  // reset preserve state
  preserve_offset_ = -1;
  preserve_mask_ = 0;
  offset_preserved_ = false;

  return v;
}

void SH4Builder::EmitDelayInstr() {
  CHECK_EQ(has_delay_instr_, true) << "No delay instruction available";
  has_delay_instr_ = false;

  (emit_callbacks[delay_instr_.type->op])(*this, fpu_state_, delay_instr_);
}

// When emitting an instruction in the delay slot, it's possible that it will
// overwrite a register needed by the original branch instruction. The branch
// emitter can request that a register be preserved with PreserveT, etc. before
// the delay slot, and if it is overwritten, the register is cached off. After
// emitting the delay slot, the branch emitter can call LoadPreserved to load
// either the original value, or if that was overwritten, the cached value.
void SH4Builder::StoreAndPreserveContext(size_t offset, Value *v,
                                         InstrFlag flags) {
  // if a register that needs to be preserved is overwritten, cache it
  if (offset == preserve_offset_) {
    CHECK(!offset_preserved_) << "Can only preserve a single value";
    StoreContext(offsetof(SH4Context, preserve),
                 LoadContext(offset, VALUE_I32));
    offset_preserved_ = true;
  }

  StoreContext(offset, v, flags);
}
