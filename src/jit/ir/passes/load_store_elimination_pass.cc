#include "emu/profiler.h"
#include "jit/ir/passes/load_store_elimination_pass.h"

using namespace dvm::jit::ir;
using namespace dvm::jit::ir::passes;

LoadStoreEliminationPass::LoadStoreEliminationPass()
    : available_(nullptr), num_available_(0) {}

void LoadStoreEliminationPass::Run(IRBuilder &builder) {
  PROFILER_RUNTIME("LoadStoreEliminationPass::Run");

  Reset();

  for (auto block : builder.blocks()) {
    ProcessBlock(block);
  }
}

void LoadStoreEliminationPass::Reset() { ClearAvailable(); }

void LoadStoreEliminationPass::ProcessBlock(Block *block) {
  // eliminate redundant loads
  {
    auto it = block->instrs().begin();
    auto end = block->instrs().end();

    ClearAvailable();

    while (it != end) {
      Instr *instr = *(it++);

      if (instr->op() == OP_LOAD_CONTEXT) {
        // if there is already a value available for this offset, reuse it and
        // remove this redundant load
        int offset = instr->arg0()->value<int32_t>();
        Value *available = GetAvailable(offset);

        if (available && available->type() == instr->result()->type()) {
          instr->result()->ReplaceRefsWith(available);
          CHECK_EQ(instr->result(), available);
          block->RemoveInstr(instr);
          continue;
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

      if (instr->op() == OP_LOAD_CONTEXT) {
        int offset = instr->arg0()->value<int32_t>();
        int size = SizeForType(instr->result()->type());

        EraseAvailable(offset, size);
      } else if (instr->op() == OP_STORE_CONTEXT) {
        // if subsequent stores have been made for this offset that would
        // overwrite it completely, mark instruction as dead
        int offset = instr->arg0()->value<int32_t>();
        Value *available = GetAvailable(offset);
        int available_size = available ? SizeForType(available->type()) : 0;
        int store_size = SizeForType(instr->arg1()->type());

        if (available_size >= store_size) {
          block->RemoveInstr(instr);
          continue;
        }

        SetAvailable(offset, instr->arg1());
      }
    }
  }
}

void LoadStoreEliminationPass::Reserve(int offset) {
  int reserve = offset + 1;

  if (reserve <= num_available_) {
    return;
  }

  // resize availability array to hold new entry
  available_ = reinterpret_cast<AvailableEntry *>(
      realloc(available_, reserve * sizeof(AvailableEntry)));

  // memset the newly allocated entries
  memset(available_ + num_available_, 0,
         (reserve - num_available_) * sizeof(AvailableEntry));

  num_available_ = reserve;
}

void LoadStoreEliminationPass::ClearAvailable() {
  if (!available_) {
    return;
  }

  memset(available_, 0, num_available_ * sizeof(AvailableEntry));
}

Value *LoadStoreEliminationPass::GetAvailable(int offset) {
  Reserve(offset);

  AvailableEntry &entry = available_[offset];

  // entries are added for the entire range of an available value to help with
  // invalidation. if this entry doesn't start at the requested offset, it's
  // not actually valid for reuse
  if (entry.offset != offset) {
    return nullptr;
  }

  return entry.value;
}

void LoadStoreEliminationPass::EraseAvailable(int offset, int size) {
  Reserve(offset + size);

  int begin = offset;
  int end = offset + size;

  // if the invalidation range intersects with an entry, merge that entry into
  // the invalidation range
  AvailableEntry &begin_entry = available_[begin];
  AvailableEntry &end_entry = available_[end];

  if (begin_entry.value) {
    begin = begin_entry.offset;
  }

  if (end_entry.value) {
    end = end_entry.offset + SizeForType(end_entry.value->type());
  }

  for (; begin < end; begin++) {
    AvailableEntry &entry = available_[begin];
    entry.offset = 0;
    entry.value = nullptr;
  }
}

void LoadStoreEliminationPass::SetAvailable(int offset, Value *v) {
  int size = SizeForType(v->type());

  Reserve(offset + size);

  EraseAvailable(offset, size);

  // add entries for the entire range to aid in invalidation. only the initial
  // entry where offset == entry.offset is valid for reuse
  for (int i = offset, end = offset + size; i < end; i++) {
    AvailableEntry &entry = available_[i];
    entry.offset = offset;
    entry.value = v;
  }
}
