#include "core/core.h"
#include "cpu/ir/passes/constant_propagation_pass.h"

using namespace dreavm;
using namespace dreavm::cpu::ir;
using namespace dreavm::cpu::ir::passes;
using namespace dreavm::emu;

// typedef void (*ConstInstrHandler)(Instr *instr);

// #define HANDLER(name)                                                 \
//   template <typename R = void, typename A0 = void, typename A1 = void, \
//             int IMM_MASK = 0>                                          \
//   static InstrResult name(RuntimeContext *ctx, Register *r, Instr *i)
// #define LOAD_ARG0() helper<A0, 0, IMM_MASK>::LoadArg(r, i)
// #define LOAD_ARG1() helper<A1, 1, IMM_MASK>::LoadArg(r, i)
// #define LOAD_ARG2() helper<A1, 2, IMM_MASK>::LoadArg(r, i)
// #define STORE_RESULT(v) SetRegister<R>(r[i->result], v);

ConstantPropagationPass::ConstantPropagationPass(Memory &memory)
    : memory_(memory) {}

void ConstantPropagationPass::Run(IRBuilder &builder) {
  for (auto block : builder.blocks()) {
    auto it = block->instrs().begin();
    auto end = block->instrs().end();

    while (it != end) {
      Instr *instr = *(it++);

      switch (instr->op()) {
        // case OP_CAST:
        //   if (instr->result()->type() == instr->arg0()->type()) {
        //     instr->result()->ReplaceRefsWith(instr->arg0());
        //     block->RemoveInstr(instr);
        //   } else if (instr->arg0()->constant()) {
        //     Value *v = nullptr;
        //     // switch (instr->result()->type) {
        //     //   case VALUE_I8:
        //     //     res = builder.AllocConstant((int8_t)instr->arg0()->value<)
        //     //     break;
        //     // }
        //   }
        //   break;

        // case OP_SEXT: {
        //   if (instr->result()->type() == instr->arg0()->type()) {
        //     instr->result()->ReplaceRefsWith(instr->arg0());
        //     block->RemoveInstr(instr);
        //   } else if (instr->arg0()->constant()) {
        //     Value *v = nullptr;
        //     switch (instr->arg0()->type()) {
        //       // BINARY_CMP(VALUE_I8, int8_t, ==)
        //       // BINARY_CMP(VALUE_I16, int16_t, ==)
        //       // BINARY_CMP(VALUE_I32, int32_t, ==)
        //       // BINARY_CMP(VALUE_I64, int64_t, ==)
        //       // BINARY_CMP(VALUE_F32, float, ==)
        //       // BINARY_CMP(VALUE_F64, double, ==)
        //       INVALID_CASE(VALUE_BLOCK)
        //     }
        //     instr->result()->ReplaceRefsWith(v);
        //     block->RemoveInstr(instr);
        //   }
        // } break;

        // case OP_ZEXT: {
        //   if (instr->result()->type() == instr->arg0()->type()) {
        //     instr->result()->ReplaceRefsWith(instr->arg0());
        //     block->RemoveInstr(instr);
        //   } else if (instr->arg0()->constant()) {
        //     Value *v = nullptr;
        //     switch (instr->arg0()->type()) {
        //       // BINARY_CMP(VALUE_I8, int8_t, ==)
        //       // BINARY_CMP(VALUE_I16, int16_t, ==)
        //       // BINARY_CMP(VALUE_I32, int32_t, ==)
        //       // BINARY_CMP(VALUE_I64, int64_t, ==)
        //       // BINARY_CMP(VALUE_F32, float, ==)
        //       // BINARY_CMP(VALUE_F64, double, ==)
        //       INVALID_CASE(VALUE_BLOCK)
        //     }
        //     instr->result()->ReplaceRefsWith(v);
        //     block->RemoveInstr(instr);
        //   }
        // } break;

        // case OP_TRUNCATE:
        //   if (instr->result()->type() == instr->arg0()->type()) {
        //   } else if (instr->arg0()->constant()) {
        //   }
        //   break;

        // case OP_SELECT:
        //   if (instr->arg0()->constant()) {
        //   }
        //   break;

        // case OP_EQ: {
        //   if (instr->arg0()->constant() && instr->arg1()->constant()) {
        //     Value *v = nullptr;
        //     switch (instr->arg0()->type()) {
        //       BINARY_CMP(VALUE_I8, int8_t, ==)
        //       BINARY_CMP(VALUE_I16, int16_t, ==)
        //       BINARY_CMP(VALUE_I32, int32_t, ==)
        //       BINARY_CMP(VALUE_I64, int64_t, ==)
        //       BINARY_CMP(VALUE_F32, float, ==)
        //       BINARY_CMP(VALUE_F64, double, ==)
        //       INVALID_CASE(VALUE_BLOCK)
        //     }
        //     instr->result()->ReplaceRefsWith(v);
        //     block->RemoveInstr(instr);
        //   }
        // } break;

        // case OP_NE: {
        //   if (instr->arg0()->constant() && instr->arg1()->constant()) {
        //     Value *v = nullptr;
        //     switch (instr->arg0()->type()) {
        //       BINARY_CMP(VALUE_I8, int8_t, !=)
        //       BINARY_CMP(VALUE_I16, int16_t, !=)
        //       BINARY_CMP(VALUE_I32, int32_t, !=)
        //       BINARY_CMP(VALUE_I64, int64_t, !=)
        //       BINARY_CMP(VALUE_F32, float, !=)
        //       BINARY_CMP(VALUE_F64, double, !=)
        //       INVALID_CASE(VALUE_BLOCK)
        //     }
        //     instr->result()->ReplaceRefsWith(v);
        //     block->RemoveInstr(instr);
        //   }
        // } break;

        // case OP_SGE:
        // case OP_SGT:
        // case OP_UGE:
        // case OP_UGT:
        // case OP_SLE:
        // case OP_SLT:
        // case OP_ULE:
        // case OP_ULT:
        // case OP_ADD:
        // case OP_SUB:
        // case OP_SMUL:
        // case OP_UMUL:
        // case OP_DIV: {
        //   if (instr->arg0()->constant() && instr->arg1()->constant()) {
        //   }
        // } break;

        // case OP_NEG:
        // case OP_SQRT:
        // case OP_ABS:
        // case OP_SIN:
        // case OP_COS:
        // case OP_NOT:
        //   if (instr->arg0()->constant()) {
        //   }
        //   break;

        // case OP_AND:
        // case OP_OR:
        // case OP_XOR:
        // case OP_SHL:
        // case OP_ASHR:
        // case OP_LSHR: {
        //   if (instr->arg0()->constant() && instr->arg1()->constant()) {
        //   }
        // } break;

        default:
          break;
      }
    }
  }
}
