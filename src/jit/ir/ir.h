#ifndef IR_BUILDER_H
#define IR_BUILDER_H

#include <stdio.h>
#include "core/assert.h"
#include "core/list.h"

typedef enum {
#define IR_OP(name) OP_##name,
#include "jit/ir/ir_ops.inc"
#undef IR_OP
  NUM_OPS
} ir_op_t;

typedef enum {
  VALUE_V,
  VALUE_I8,
  VALUE_I16,
  VALUE_I32,
  VALUE_I64,
  VALUE_F32,
  VALUE_F64,
  VALUE_V128,
  VALUE_NUM,
} ir_type_t;

typedef enum {
  CMP_EQ,
  CMP_NE,
  CMP_SGE,
  CMP_SGT,
  CMP_UGE,
  CMP_UGT,
  CMP_SLE,
  CMP_SLT,
  CMP_ULE,
  CMP_ULT
} ir_cmp_t;

struct ir_value_s;
struct ir_instr_s;

static const int MAX_INSTR_ARGS = 3;

// use is a layer of indirection between an instruction and the values it uses
// as arguments. this indirection makes it possible to maintain a list for each
// value of the arguments that reference it
typedef struct ir_use_s {
  // the instruction that's using the value
  struct ir_instr_s *instr;

  // pointer to the argument that's using the value. this is used to substitute
  // a new value for the argument in the case that the original value is
  // removed (e.g. due to constant propagation)
  struct ir_value_s **parg;

  list_node_t it;
} ir_use_t;

typedef struct ir_value_s {
  ir_type_t type;

  union {
    int8_t i8;
    int16_t i16;
    int32_t i32;
    int64_t i64;
    float f32;
    double f64;
  };

  // instruction that defines this value (non-constant values)
  struct ir_instr_s *def;

  // instructions that use this value as an argument
  list_t uses;

  // host register allocated for this value
  int reg;

  // generic meta data used by optimization passes
  intptr_t tag;
} ir_value_t;

typedef struct ir_instr_s {
  ir_op_t op;

  // values used by each argument. note, the argument / use is split into two
  // separate members to ease reading the argument value (instr->arg[0] vs
  // instr->arg[0].value)
  ir_value_t *arg[MAX_INSTR_ARGS];
  ir_use_t used[MAX_INSTR_ARGS];

  // result of the instruction. note, instruction results don't consider
  // themselves users of the value (eases register allocation logic)
  ir_value_t *result;

  // generic meta data used by optimization passes
  intptr_t tag;

  list_node_t it;
} ir_instr_t;

// locals are allocated for values that need to be spilled to the stack
// during register allocation
typedef struct ir_local_s {
  ir_type_t type;
  ir_value_t *offset;
  list_node_t it;
} ir_local_t;

typedef struct ir_s {
  uint8_t *buffer;
  int capacity;
  int used;

  list_t instrs;
  list_t locals;
  int locals_size;

  ir_instr_t *current_instr;
} ir_t;

extern const char *ir_op_names[NUM_OPS];

static const int VALUE_I8_MASK = 1 << VALUE_I8;
static const int VALUE_I16_MASK = 1 << VALUE_I16;
static const int VALUE_I32_MASK = 1 << VALUE_I32;
static const int VALUE_I64_MASK = 1 << VALUE_I64;
static const int VALUE_F32_MASK = 1 << VALUE_F32;
static const int VALUE_F64_MASK = 1 << VALUE_F64;
static const int VALUE_V128_MASK = 1 << VALUE_V128;
static const int VALUE_INT_MASK =
    VALUE_I8_MASK | VALUE_I16_MASK | VALUE_I32_MASK | VALUE_I64_MASK;
static const int VALUE_FLOAT_MASK = VALUE_F32_MASK | VALUE_F64_MASK;
static const int VALUE_VECTOR_MASK = VALUE_V128_MASK;
static const int VALUE_ALL_MASK = VALUE_INT_MASK | VALUE_FLOAT_MASK;

static const int NO_REGISTER = -1;

static inline int ir_type_size(ir_type_t type) {
  switch (type) {
    case VALUE_I8:
      return 1;
    case VALUE_I16:
      return 2;
    case VALUE_I32:
      return 4;
    case VALUE_I64:
      return 8;
    case VALUE_F32:
      return 4;
    case VALUE_F64:
      return 8;
    case VALUE_V128:
      return 16;
    default:
      LOG_FATAL("Unexpected value type");
      break;
  }
}

static inline bool is_is_int(ir_type_t type) {
  return type == VALUE_I8 || type == VALUE_I16 || type == VALUE_I32 ||
         type == VALUE_I64;
}

static inline bool ir_is_float(ir_type_t type) {
  return type == VALUE_F32 || type == VALUE_F64;
}

static inline bool ir_is_vector(ir_type_t type) {
  return type == VALUE_V128;
}

bool ir_read(FILE *input, struct ir_s *ir);
void ir_write(struct ir_s *ir, FILE *output);

ir_instr_t *ir_append_instr(ir_t *ir, ir_op_t op, ir_type_t result_type);
void ir_remove_instr(ir_t *ir, ir_instr_t *instr);

ir_value_t *ir_alloc_i8(ir_t *ir, int8_t c);
ir_value_t *ir_alloc_i16(ir_t *ir, int16_t c);
ir_value_t *ir_alloc_i32(ir_t *ir, int32_t c);
ir_value_t *ir_alloc_i64(ir_t *ir, int64_t c);
ir_value_t *ir_alloc_f32(ir_t *ir, float c);
ir_value_t *ir_alloc_f64(ir_t *ir, double c);
ir_local_t *ir_alloc_local(ir_t *ir, ir_type_t type);

void ir_set_arg(ir_t *ir, ir_instr_t *instr, int n, ir_value_t *v);
void ir_set_arg0(ir_t *ir, ir_instr_t *instr, ir_value_t *v);
void ir_set_arg1(ir_t *ir, ir_instr_t *instr, ir_value_t *v);
void ir_set_arg2(ir_t *ir, ir_instr_t *instr, ir_value_t *v);

void ir_replace_use(ir_use_t *use, ir_value_t *other);
void ir_replace_uses(ir_value_t *v, ir_value_t *other);

bool ir_is_constant(const ir_value_t *v);
uint64_t ir_zext_constant(const ir_value_t *v);

// direct access to host memory
ir_value_t *ir_load_host(ir_t *ir, ir_value_t *addr, ir_type_t type);
void ir_store_host(ir_t *ir, ir_value_t *addr, ir_value_t *v);

// guest memory operations
ir_value_t *ir_load_fast(ir_t *ir, ir_value_t *addr, ir_type_t type);
void ir_store_fast(ir_t *ir, ir_value_t *addr, ir_value_t *v);

ir_value_t *ir_load_slow(ir_t *ir, ir_value_t *addr, ir_type_t type);
void ir_store_slow(ir_t *ir, ir_value_t *addr, ir_value_t *v);

// context operations
ir_value_t *ir_load_context(ir_t *ir, size_t offset, ir_type_t type);
void ir_store_context(ir_t *ir, size_t offset, ir_value_t *v);

// local operations
ir_value_t *ir_load_local(ir_t *ir, ir_local_t *local);
void ir_store_local(ir_t *ir, ir_local_t *local, ir_value_t *v);

// cast / conversion operations
ir_value_t *ir_ftoi(ir_t *ir, ir_value_t *v, ir_type_t dest_type);
ir_value_t *ir_itof(ir_t *ir, ir_value_t *v, ir_type_t dest_type);
ir_value_t *ir_sext(ir_t *ir, ir_value_t *v, ir_type_t dest_type);
ir_value_t *ir_zext(ir_t *ir, ir_value_t *v, ir_type_t dest_type);
ir_value_t *ir_trunc(ir_t *ir, ir_value_t *v, ir_type_t dest_type);
ir_value_t *ir_fext(ir_t *ir, ir_value_t *v, ir_type_t dest_type);
ir_value_t *ir_ftrunc(ir_t *ir, ir_value_t *v, ir_type_t dest_type);

// conditionals
ir_value_t *ir_select(ir_t *ir, ir_value_t *cond, ir_value_t *t, ir_value_t *f);
ir_value_t *ir_cmp_eq(ir_t *ir, ir_value_t *a, ir_value_t *b);
ir_value_t *ir_cmp_ne(ir_t *ir, ir_value_t *a, ir_value_t *b);
ir_value_t *ir_cmp_sge(ir_t *ir, ir_value_t *a, ir_value_t *b);
ir_value_t *ir_cmp_sgt(ir_t *ir, ir_value_t *a, ir_value_t *b);
ir_value_t *ir_cmp_uge(ir_t *ir, ir_value_t *a, ir_value_t *b);
ir_value_t *ir_cmp_ugt(ir_t *ir, ir_value_t *a, ir_value_t *b);
ir_value_t *ir_cmp_sle(ir_t *ir, ir_value_t *a, ir_value_t *b);
ir_value_t *ir_cmp_slt(ir_t *ir, ir_value_t *a, ir_value_t *b);
ir_value_t *ir_cmp_ule(ir_t *ir, ir_value_t *a, ir_value_t *b);
ir_value_t *ir_cmp_ult(ir_t *ir, ir_value_t *a, ir_value_t *b);
ir_value_t *ir_fcmp_eq(ir_t *ir, ir_value_t *a, ir_value_t *b);
ir_value_t *ir_fcmp_ne(ir_t *ir, ir_value_t *a, ir_value_t *b);
ir_value_t *ir_fcmp_ge(ir_t *ir, ir_value_t *a, ir_value_t *b);
ir_value_t *ir_fcmp_gt(ir_t *ir, ir_value_t *a, ir_value_t *b);
ir_value_t *ir_fcmp_le(ir_t *ir, ir_value_t *a, ir_value_t *b);
ir_value_t *ir_fcmp_lt(ir_t *ir, ir_value_t *a, ir_value_t *b);

// integer math operators
ir_value_t *ir_add(ir_t *ir, ir_value_t *a, ir_value_t *b);
ir_value_t *ir_sub(ir_t *ir, ir_value_t *a, ir_value_t *b);
ir_value_t *ir_smul(ir_t *ir, ir_value_t *a, ir_value_t *b);
ir_value_t *ir_umul(ir_t *ir, ir_value_t *a, ir_value_t *b);
ir_value_t *ir_div(ir_t *ir, ir_value_t *a, ir_value_t *b);
ir_value_t *ir_neg(ir_t *ir, ir_value_t *a);
ir_value_t *ir_abs(ir_t *ir, ir_value_t *a);

// floating point math operators
ir_value_t *ir_fadd(ir_t *ir, ir_value_t *a, ir_value_t *b);
ir_value_t *ir_fsub(ir_t *ir, ir_value_t *a, ir_value_t *b);
ir_value_t *ir_fmul(ir_t *ir, ir_value_t *a, ir_value_t *b);
ir_value_t *ir_fdiv(ir_t *ir, ir_value_t *a, ir_value_t *b);
ir_value_t *ir_fneg(ir_t *ir, ir_value_t *a);
ir_value_t *ir_fabs(ir_t *ir, ir_value_t *a);
ir_value_t *ir_sqrt(ir_t *ir, ir_value_t *a);

// vector math operators
ir_value_t *ir_vbroadcast(ir_t *ir, ir_value_t *a);
ir_value_t *ir_vadd(ir_t *ir, ir_value_t *a, ir_value_t *b, ir_type_t el_type);
ir_value_t *ir_vdot(ir_t *ir, ir_value_t *a, ir_value_t *b, ir_type_t el_type);
ir_value_t *ir_vmul(ir_t *ir, ir_value_t *a, ir_value_t *b, ir_type_t el_type);

// bitwise operations
ir_value_t *ir_and(ir_t *ir, ir_value_t *a, ir_value_t *b);
ir_value_t *ir_or(ir_t *ir, ir_value_t *a, ir_value_t *b);
ir_value_t *ir_xor(ir_t *ir, ir_value_t *a, ir_value_t *b);
ir_value_t *ir_not(ir_t *ir, ir_value_t *a);
ir_value_t *ir_shl(ir_t *ir, ir_value_t *a, ir_value_t *n);
ir_value_t *ir_shli(ir_t *ir, ir_value_t *a, int n);
ir_value_t *ir_ashr(ir_t *ir, ir_value_t *a, ir_value_t *n);
ir_value_t *ir_ashri(ir_t *ir, ir_value_t *a, int n);
ir_value_t *ir_lshr(ir_t *ir, ir_value_t *a, ir_value_t *n);
ir_value_t *ir_lshri(ir_t *ir, ir_value_t *a, int n);
ir_value_t *ir_ashd(ir_t *ir, ir_value_t *a, ir_value_t *n);
ir_value_t *ir_lshd(ir_t *ir, ir_value_t *a, ir_value_t *n);

// branches
void ir_branch(ir_t *ir, ir_value_t *dest);
void ir_branch_cond(ir_t *ir, ir_value_t *cond, ir_value_t *true_addr,
                    ir_value_t *false_addr);

// calls
void ir_call_external_1(ir_t *ir, ir_value_t *addr);
void ir_call_external_2(ir_t *ir, ir_value_t *addr, ir_value_t *arg0);

#endif
