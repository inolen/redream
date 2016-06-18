#include <type_traits>
#include <unordered_map>
#include "jit/ir/passes/constant_propagation_pass.h"

typedef void (*FoldFn)(IRBuilder &, Instr *i);

// specify which arguments must be constant in order for fold operation to run
enum {
  ARG0_CNST = 0x1,
  ARG1_CNST = 0x2,
  ARG2_CNST = 0x4,
};

// fold callbacks for each operaton
std::unordered_map<int, FoldFn> fold_cbs;
int fold_masks[NUM_OPS];

// OP_SELECT and OP_BRANCH_COND are the only instructions using arg2, and
// arg2's type always matches arg1's. because of this, arg2 isn't considered
// when generating the lookup table
#define CALLBACK_IDX(op, r, a0, a1)                                        \
  ((op)*VALUE_NUM * VALUE_NUM * VALUE_NUM) + ((r)*VALUE_NUM * VALUE_NUM) + \
      ((a0)*VALUE_NUM) + (a1)

// declare a templated callback for an IR operation. note, declaring a
// callback does not actually register it. callbacks must be registered
// for a particular signature with REGISTER_FOLD
#define FOLD(op, mask)                                  \
  static struct _##op##_init {                          \
    _##op##_init() {                                    \
      fold_masks[OP_##op] = mask;                       \
    }                                                   \
  } op##_init;                                          \
  template <typename R = struct ir_valueInfo<VALUE_V>,  \
            typename A0 = struct ir_valueInfo<VALUE_V>, \
            typename A1 = struct ir_valueInfo<VALUE_V>> \
  void Handle##op(struct ir *ir, Instr *instr)

// registers a fold callback for the specified signature
#define REGISTER_FOLD(op, r, a0, a1)                                       \
  static struct _cpp_##op##_##r##_##a0##_##a1##_init {                     \
    _cpp_##op##_##r##_##a0##_##a1##_init() {                               \
      fold_cbs[CALLBACK_IDX(OP_##op, VALUE_##r, VALUE_##a0, VALUE_##a1)] = \
          &Handle##op<struct ir_valueInfo<VALUE_##r>,                      \
                      struct ir_valueInfo<VALUE_##a0>,                     \
                      struct ir_valueInfo<VALUE_##a1>>;                    \
    }                                                                      \
  } cpp_##op##_##r##_##a0##_##a1##_init

// common helpers for fold functions
#define ARG0() (instr->arg[0]->*A0::fn)()
#define ARG1() (instr->arg[1]->*A1::fn)()
#define ARG2() (instr->arg[2]->*A1::fn)()
#define ARG0_UNSIGNED() static_cast<typename A0::unsigned_type>(ARG0())
#define ARG1_UNSIGNED() static_cast<typename A1::unsigned_type>(ARG1())
#define ARG2_UNSIGNED() static_cast<typename A1::unsigned_type>(ARG2())
#define RESULT(expr)                                                           \
  ir_replace_uses(instr, ir_alloc_constant(                                    \
                             ir, static_cast<typename R::signed_type>(expr))); \
  ir_remove_instr(instr)

static FoldFn GetFoldFn(Instr *instr) {
  auto it = fold_cbs.find(
      CALLBACK_IDX(instr->op, instr->type,
                   instr->arg[0] ? (int)instr->arg[0]->type : VALUE_V,
                   instr->arg[1] ? (int)instr->arg[1]->type : VALUE_V));
  if (it == fold_cbs.end()) {
    return nullptr;
  }
  return it->second;
}

static int GetFoldMask(Instr *instr) {
  return fold_masks[instr->op];
}

static int GetConstantSig(Instr *instr) {
  int cnst_sig = 0;

  if (instr->arg[0] && ir_is_constant(instr->arg[0])) {
    cnst_sig |= ARG0_CNST;
  }

  if (instr->arg[1] && ir_is_constant(instr->arg[1])) {
    cnst_sig |= ARG1_CNST;
  }

  if (instr->arg[2] && ir_is_constant(instr->arg[2])) {
    cnst_sig |= ARG2_CNST;
  }

  return cnst_sig;
}

void ConstantPropagationPass::Run(struct ir *ir) {
  list_for_each_entry_safe(instr, &ir->instrs, struct ir_instr, it) {
    int fold_mask = GetFoldMask(instr);
    int cnst_sig = GetConstantSig(instr);
    if (!fold_mask || (cnst_sig & fold_mask) != fold_mask) {
      continue;
    }

    FoldFn fold = GetFoldFn(instr);
    if (!fold) {
      continue;
    }

    fold(builder, instr);
  }
}

FOLD(SELECT, ARG0_CNST) {
  ir_replace_uses(instr, ARG0() ? instr->arg[1] : instr->arg[2]);
  ir_remove_instr(ir, instr);
}
REGISTER_FOLD(SELECT, I8, I8, I8);
REGISTER_FOLD(SELECT, I16, I16, I16);
REGISTER_FOLD(SELECT, I32, I32, I32);
REGISTER_FOLD(SELECT, I64, I64, I64);

FOLD(EQ, ARG0_CNST | ARG1_CNST) {
  RESULT(ARG0() == ARG1());
}
REGISTER_FOLD(EQ, I8, I8, I8);
REGISTER_FOLD(EQ, I8, I16, I16);
REGISTER_FOLD(EQ, I8, I32, I32);
REGISTER_FOLD(EQ, I8, I64, I64);
REGISTER_FOLD(EQ, I8, F32, F32);
REGISTER_FOLD(EQ, I8, F64, F64);

FOLD(NE, ARG0_CNST | ARG1_CNST) {
  RESULT(ARG0() != ARG1());
}
REGISTER_FOLD(NE, I8, I8, I8);
REGISTER_FOLD(NE, I8, I16, I16);
REGISTER_FOLD(NE, I8, I32, I32);
REGISTER_FOLD(NE, I8, I64, I64);
REGISTER_FOLD(NE, I8, F32, F32);
REGISTER_FOLD(NE, I8, F64, F64);

FOLD(SGE, ARG0_CNST | ARG1_CNST) {
  RESULT(ARG0() >= ARG1());
}
REGISTER_FOLD(SGE, I8, I8, I8);
REGISTER_FOLD(SGE, I8, I16, I16);
REGISTER_FOLD(SGE, I8, I32, I32);
REGISTER_FOLD(SGE, I8, I64, I64);
REGISTER_FOLD(SGE, I8, F32, F32);
REGISTER_FOLD(SGE, I8, F64, F64);

FOLD(SGT, ARG0_CNST | ARG1_CNST) {
  RESULT(ARG0() > ARG1());
}
REGISTER_FOLD(SGT, I8, I8, I8);
REGISTER_FOLD(SGT, I8, I16, I16);
REGISTER_FOLD(SGT, I8, I32, I32);
REGISTER_FOLD(SGT, I8, I64, I64);
REGISTER_FOLD(SGT, I8, F32, F32);
REGISTER_FOLD(SGT, I8, F64, F64);

// IR_OP(UGE)
// IR_OP(UGT)

FOLD(SLE, ARG0_CNST | ARG1_CNST) {
  RESULT(ARG0() <= ARG1());
}
REGISTER_FOLD(SLE, I8, I8, I8);
REGISTER_FOLD(SLE, I8, I16, I16);
REGISTER_FOLD(SLE, I8, I32, I32);
REGISTER_FOLD(SLE, I8, I64, I64);
REGISTER_FOLD(SLE, I8, F32, F32);
REGISTER_FOLD(SLE, I8, F64, F64);

FOLD(SLT, ARG0_CNST | ARG1_CNST) {
  RESULT(ARG0() < ARG1());
}
REGISTER_FOLD(SLT, I8, I8, I8);
REGISTER_FOLD(SLT, I8, I16, I16);
REGISTER_FOLD(SLT, I8, I32, I32);
REGISTER_FOLD(SLT, I8, I64, I64);
REGISTER_FOLD(SLT, I8, F32, F32);
REGISTER_FOLD(SLT, I8, F64, F64);

// IR_OP(ULE)
// IR_OP(ULT)

FOLD(ADD, ARG0_CNST | ARG1_CNST) {
  RESULT(ARG0() + ARG1());
}
REGISTER_FOLD(ADD, I8, I8, I8);
REGISTER_FOLD(ADD, I16, I16, I16);
REGISTER_FOLD(ADD, I32, I32, I32);
REGISTER_FOLD(ADD, I64, I64, I64);
REGISTER_FOLD(ADD, F32, F32, F32);
REGISTER_FOLD(ADD, F64, F64, F64);

FOLD(SUB, ARG0_CNST | ARG1_CNST) {
  RESULT(ARG0() - ARG1());
}
REGISTER_FOLD(SUB, I8, I8, I8);
REGISTER_FOLD(SUB, I16, I16, I16);
REGISTER_FOLD(SUB, I32, I32, I32);
REGISTER_FOLD(SUB, I64, I64, I64);
REGISTER_FOLD(SUB, F32, F32, F32);
REGISTER_FOLD(SUB, F64, F64, F64);

FOLD(SMUL, ARG0_CNST | ARG1_CNST) {
  RESULT(ARG0() * ARG1());
}
REGISTER_FOLD(SMUL, I8, I8, I8);
REGISTER_FOLD(SMUL, I16, I16, I16);
REGISTER_FOLD(SMUL, I32, I32, I32);
REGISTER_FOLD(SMUL, I64, I64, I64);
REGISTER_FOLD(SMUL, F32, F32, F32);
REGISTER_FOLD(SMUL, F64, F64, F64);

FOLD(UMUL, ARG0_CNST | ARG1_CNST) {
  auto lhs = ARG0_UNSIGNED();
  auto rhs = ARG1_UNSIGNED();
  RESULT(lhs * rhs);
}
REGISTER_FOLD(UMUL, I8, I8, I8);
REGISTER_FOLD(UMUL, I16, I16, I16);
REGISTER_FOLD(UMUL, I32, I32, I32);
REGISTER_FOLD(UMUL, I64, I64, I64);

// IR_OP(DIV)
// IR_OP(NEG)
// IR_OP(SQRT)
// IR_OP(ABS)
// IR_OP(SIN)
// IR_OP(COS)

FOLD(AND, ARG0_CNST | ARG1_CNST) {
  RESULT(ARG0() & ARG1());
}
REGISTER_FOLD(AND, I8, I8, I8);
REGISTER_FOLD(AND, I16, I16, I16);
REGISTER_FOLD(AND, I32, I32, I32);
REGISTER_FOLD(AND, I64, I64, I64);

FOLD(OR, ARG0_CNST | ARG1_CNST) {
  RESULT(ARG0() | ARG1());
}
REGISTER_FOLD(OR, I8, I8, I8);
REGISTER_FOLD(OR, I16, I16, I16);
REGISTER_FOLD(OR, I32, I32, I32);
REGISTER_FOLD(OR, I64, I64, I64);

FOLD(XOR, ARG0_CNST | ARG1_CNST) {
  RESULT(ARG0() ^ ARG1());
}
REGISTER_FOLD(XOR, I8, I8, I8);
REGISTER_FOLD(XOR, I16, I16, I16);
REGISTER_FOLD(XOR, I32, I32, I32);
REGISTER_FOLD(XOR, I64, I64, I64);

FOLD(NOT, ARG0_CNST) {
  RESULT(~ARG0());
}
REGISTER_FOLD(NOT, I8, I8, V);
REGISTER_FOLD(NOT, I16, I16, V);
REGISTER_FOLD(NOT, I32, I32, V);
REGISTER_FOLD(NOT, I64, I64, V);

FOLD(SHL, ARG0_CNST | ARG1_CNST) {
  RESULT(ARG0() << ARG1());
}
REGISTER_FOLD(SHL, I8, I8, I32);
REGISTER_FOLD(SHL, I16, I16, I32);
REGISTER_FOLD(SHL, I32, I32, I32);
REGISTER_FOLD(SHL, I64, I64, I32);

// IR_OP(ASHR)

FOLD(LSHR, ARG0_CNST | ARG1_CNST) {
  RESULT(ARG0_UNSIGNED() >> ARG1());
}
REGISTER_FOLD(LSHR, I8, I8, I32);
REGISTER_FOLD(LSHR, I16, I16, I32);
REGISTER_FOLD(LSHR, I32, I32, I32);
REGISTER_FOLD(LSHR, I64, I64, I32);

// IR_OP(BRANCH)
// IR_OP(BRANCH_COND)
// IR_OP(CALL_EXTERNAL)
