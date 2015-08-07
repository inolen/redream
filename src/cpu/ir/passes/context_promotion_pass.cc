#include "core/core.h"
#include "cpu/ir/passes/context_promotion_pass.h"
#include "emu/profiler.h"

using namespace dreavm::cpu::ir;
using namespace dreavm::cpu::ir::passes;

void ContextPromotionPass::Run(IRBuilder &builder) {
  PROFILER_SCOPE("runtime", "ContextPromotionPass::Run");

  ResetState();

  for (auto block : builder.blocks()) {
    ProcessBlock(block);
  }
}

void ContextPromotionPass::ResetState() {
  ClearAvailable();
}

void ContextPromotionPass::ProcessBlock(Block *block) {
  // eliminate redundant loads
  {
    auto it = block->instrs().begin();
    auto end = block->instrs().end();

    ClearAvailable();

    while (it != end) {
      Instr *instr = *(it++);

      if (instr->flags() & IF_INVALIDATE_CONTEXT) {
        // if the instruction explicitly invalidates the context, clear
        // available values
        ClearAvailable();
      } else if (instr->op() == OP_LOAD_CONTEXT) {
        // if there is already a value available for this offset, reuse it and
        // remove this redundant load
        int offset = instr->arg0()->value<int32_t>();
        Value *available = GetAvailable(offset);

        if (available && available->type() == instr->result()->type()) {
          instr->result()->ReplaceRefsWith(available);
          block->RemoveInstr(instr);
        }

        SetAvailable(offset, instr->result());
      } else if (instr->op() == OP_STORE_CONTEXT) {
        int offset = instr->arg0()->value<int32_t>();

        // mark the value being stored as available
        SetAvailable(offset, instr->arg1());
      }
    }
  }

  // eliminate dead stores
  {
    // iterate in reverse so the current instruction is the one being removed
    auto it = block->instrs().rbegin();
    auto end = block->instrs().rend();

    ClearAvailable();

    while (it != end) {
      Instr *instr = *(it++);

      if (instr->flags() & IF_INVALIDATE_CONTEXT) {
        ClearAvailable();
      } else if (instr->op() == OP_LOAD_CONTEXT) {
        int offset = instr->arg0()->value<int32_t>();
        SetAvailable(offset, nullptr);
      } else if (instr->op() == OP_STORE_CONTEXT) {
        // if subsequent stores have been made for this offset, this instruction
        // is dead
        int offset = instr->arg0()->value<int32_t>();
        Value *available = GetAvailable(offset);
        if (available && available->type() >= instr->arg1()->type()) {
          block->RemoveInstr(instr);
        }
        SetAvailable(offset, instr->arg1());
      }
    }
  }
}

void ContextPromotionPass::ClearAvailable() {
  available_marker_++;
}

void ContextPromotionPass::ReserveAvailable(int offset) {
  if (offset >= (int)available_.size()) {
    available_.resize(offset + 1);
    available_values_.resize(offset + 1);
  }
}

Value *ContextPromotionPass::GetAvailable(int offset) {
  ReserveAvailable(offset);

  if (available_[offset] < available_marker_) {
    return nullptr;
  }

  return available_values_[offset];
}

void ContextPromotionPass::SetAvailable(int offset, Value *v) {
  ReserveAvailable(offset);

  available_[offset] = available_marker_;
  available_values_[offset] = v;
}
