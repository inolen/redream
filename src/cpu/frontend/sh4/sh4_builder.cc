#include "cpu/frontend/sh4/sh4_emit.h"
#include "cpu/frontend/sh4/sh4_frontend.h"
#include "cpu/frontend/sh4/sh4_instr.h"
#include "cpu/runtime.h"

using namespace dreavm::cpu;
using namespace dreavm::cpu::frontend;
using namespace dreavm::cpu::frontend::sh4;
using namespace dreavm::cpu::ir;
using namespace dreavm::emu;

void SRUpdated(SH4Context *ctx) { ctx->SRUpdated(); }

void FPSCRUpdated(SH4Context *ctx) { ctx->FPSCRUpdated(); }

SH4Builder::SH4Builder(Memory &memory)
    : memory_(memory), has_delay_instr_(false), last_instr_(nullptr) {}

SH4Builder::~SH4Builder() {}

void SH4Builder::Emit(uint32_t start_addr) {
  uint32_t addr = start_addr;
  int guest_cycles = 0;

  while (true) {
    Instr instr(addr, memory_.R16(addr));
    bool delayed = instr.type->flags & OP_FLAG_DELAYED;

    guest_cycles += instr.type->cycles;

    // save off the delay instruction if we need to
    if (delayed) {
      delay_instr_ = Instr(addr + 2, memory_.R16(addr + 2));
      has_delay_instr_ = true;

      guest_cycles += delay_instr_.type->cycles;
    }

    // emit the current instruction
    (emit_callbacks[instr.type->op])(*this, instr);

    // find the first ir instruction emitted for this op
    ir::Instr *emitted = GetFirstEmittedInstr();
    if (emitted) {
      emitted->guest_addr = addr;
      emitted->guest_op = instr.type->op;
    }

    // delayed instructions will be emitted already by the instructions handler
    addr += delayed ? 4 : 2;

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
      tail_instr->set_arg1(AllocConstant(static_cast<int32_t>(addr)));
    } else if (tail_instr->arg2()->type() == VALUE_BLOCK &&
               tail_instr->arg2()->value<Block *>() == last_block) {
      tail_instr->set_arg2(AllocConstant(static_cast<int32_t>(addr)));
    }

    RemoveBlock(last_block);
  }

  // store off guest cycles approximation for this block
  SetMetadata(MD_GUEST_CYCLES, AllocConstant(guest_cycles));
}

void SH4Builder::DumpToFile(uint32_t start_addr) {
  char filename[PATH_MAX];
  snprintf(filename, sizeof(filename), "../dreamcast/0x%x.bin", start_addr);

  printf("DUMPING 0x%x to %s\n", start_addr, filename);

  FILE *fp = fopen(filename, "wb");
  if (!fp) {
    printf("FAILED TO OPEN FILE HANDLE\n");
    return;
  }

  uint32_t addr = start_addr;
  while (true) {
    uint16_t opcode = memory_.R16(addr);
    Instr instr(addr, opcode);
    bool delayed = instr.type->flags & OP_FLAG_DELAYED;

    fwrite(&opcode, 2, 1, fp);

    if (delayed) {
      uint16_t delay_opcode = memory_.R16(addr + 2);
      fwrite(&delay_opcode, 2, 1, fp);
    }

    if (instr.type->flags & OP_FLAG_BRANCH) {
      break;
    }

    addr += delayed ? 4 : 2;
  }

  fclose(fp);
}

Value *SH4Builder::LoadRegister(int n, ValueTy type) {
  return LoadContext(offsetof(SH4Context, r[n]), type);
}

void SH4Builder::StoreRegister(int n, Value *v) {
  CHECK_EQ(v->type(), VALUE_I32);
  return StoreContext(offsetof(SH4Context, r[n]), v);
}

Value *SH4Builder::LoadRegisterF(int n, ValueTy type) {
  return LoadContext(offsetof(SH4Context, fr[n]), type);
}

void SH4Builder::StoreRegisterF(int n, Value *v) {
  return StoreContext(offsetof(SH4Context, fr[n]), v);
}

Value *SH4Builder::LoadRegisterXF(int n, ValueTy type) {
  return LoadContext(offsetof(SH4Context, xf[n]), type);
}

void SH4Builder::StoreRegisterXF(int n, Value *v) {
  return StoreContext(offsetof(SH4Context, xf[n]), v);
}

Value *SH4Builder::LoadSR() {
  return LoadContext(offsetof(SH4Context, sr), VALUE_I32);
}

void SH4Builder::StoreSR(Value *v) {
  CHECK_EQ(v->type(), VALUE_I32);
  StoreContext(offsetof(SH4Context, sr), v, IF_INVALIDATE_CONTEXT);
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
  StoreContext(offsetof(SH4Context, gbr), v);
}

ir::Value *SH4Builder::LoadFPSCR() {
  ir::Value *v = LoadContext(offsetof(SH4Context, fpscr), VALUE_I32);
  v = And(v, AllocConstant(0x003fffff));
  return v;
}

void SH4Builder::StoreFPSCR(ir::Value *v) {
  CHECK_EQ(v->type(), VALUE_I32);
  v = And(v, AllocConstant(0x003fffff));
  StoreContext(offsetof(SH4Context, fpscr), v);
  CallExternal((ExternalFn)&FPSCRUpdated);
}

void SH4Builder::EmitDelayInstr() {
  CHECK_EQ(has_delay_instr_, true) << "No delay instruction available";
  has_delay_instr_ = false;

  (emit_callbacks[delay_instr_.type->op])(*this, delay_instr_);

  ir::Instr *emitted = GetFirstEmittedInstr();
  if (emitted) {
    emitted->guest_addr = delay_instr_.addr;
    emitted->guest_op = delay_instr_.type->op;
  }
}

// FIXME simplify this
ir::Instr *SH4Builder::GetFirstEmittedInstr() {
  ir::Instr *first = last_instr_;

  // find the first instruction since the tail of the last batch
  if (!first && current_block_ && current_block_->instrs().head()) {
    first = current_block_->instrs().head();
  } else if (first && first->next()) {
    first = first->next();
  } else if (first && first->block()->next() &&
             first->block()->next()->instrs().head()) {
    first = first->block()->next()->instrs().head();
  }

  // nothing new emitted
  if (first == last_instr_) {
    return nullptr;
  }

  // move to end of this batch of emitted instructions
  last_instr_ = first;
  if (last_instr_) {
    while (last_instr_->block()->next() &&
           last_instr_->block()->next()->instrs().head()) {
      last_instr_ = last_instr_->block()->next()->instrs().head();
    }
    while (last_instr_->next()) {
      last_instr_ = last_instr_->next();
    }
  }
  CHECK_NOTNULL(last_instr_);

  return first;
}
