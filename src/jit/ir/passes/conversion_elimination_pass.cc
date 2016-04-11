#include "jit/ir/passes/conversion_elimination_pass.h"

using namespace re::jit::backend;
using namespace re::jit::ir;
using namespace re::jit::ir::passes;

DEFINE_STAT(num_sext_removed, "Number of sext eliminated");
DEFINE_STAT(num_zext_removed, "Number of zext eliminated");
DEFINE_STAT(num_trunc_removed, "Number of trunc eliminated");

void ConversionEliminationPass::Run(IRBuilder &builder) {
  auto it = builder.instrs().begin();
  auto end = builder.instrs().end();

  while (it != end) {
    Instr *instr = *(it++);

    // eliminate unnecessary sext / zext operations
    if (instr->op() == OP_LOAD_HOST || instr->op() == OP_LOAD_GUEST ||
        instr->op() == OP_LOAD_CONTEXT) {
      ValueType memory_type = VALUE_V;
      bool same_type = true;
      bool all_sext = true;
      bool all_zext = true;

      for (auto use : instr->uses()) {
        Instr *use_instr = use->instr();

        if (use_instr->op() == OP_SEXT || use_instr->op() == OP_ZEXT) {
          if (memory_type == VALUE_V) {
            memory_type = use_instr->type();
          }

          if (memory_type != use_instr->type()) {
            same_type = false;
          }
        }

        if (use_instr->op() != OP_SEXT) {
          all_sext = false;
        }

        if (use_instr->op() != OP_ZEXT) {
          all_zext = false;
        }
      }

      if (same_type && all_sext) {
        // TODO implement

        num_sext_removed++;
      } else if (same_type && all_zext) {
        // TODO implement

        num_zext_removed++;
      }
    } else if (instr->op() == OP_STORE_HOST || instr->op() == OP_STORE_GUEST ||
               instr->op() == OP_STORE_CONTEXT) {
      Value *store_value = instr->arg1();

      if (!store_value->constant()) {
        Instr *def = store_value->def();

        if (def->op() == OP_TRUNC) {
          // TODO implement

          // note, don't actually remove the truncation as other values may
          // reference it. let DCE clean it up
          num_trunc_removed++;
        }
      }
    }
  }
}
