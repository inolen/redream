#include "core/math.h"
#include "jit/ir/ir.h"

const char *ir_op_names[NUM_OPS] = {
#define IR_OP(name) #name,
#include "jit/ir/ir_ops.inc"
};

static void *ir_calloc(ir_t *ir, int size) {
  CHECK_LE(ir->used + size, ir->capacity);
  uint8_t *ptr = ir->buffer + ir->used;
  memset(ptr, 0, size);
  ir->used += size;
  return ptr;
}

static ir_instr_t *ir_alloc_instr(ir_t *ir, ir_op_t op) {
  ir_instr_t *instr = ir_calloc(ir, sizeof(ir_instr_t));

  instr->op = op;

  // initialize use links
  for (int i = 0; i < MAX_INSTR_ARGS; i++) {
    ir_use_t *use = &instr->used[i];
    use->instr = instr;
    use->parg = &instr->arg[i];
  }

  return instr;
}

static void ir_add_use(ir_value_t *v, ir_use_t *use) {
  list_add(&v->uses, &use->it);
}

static void ir_remove_use(ir_value_t *v, ir_use_t *use) {
  list_remove(&v->uses, &use->it);
}

ir_instr_t *ir_append_instr(ir_t *ir, ir_op_t op, ir_type_t result_type) {
  ir_instr_t *instr = ir_alloc_instr(ir, op);

  // allocate result if needed
  if (result_type != VALUE_V) {
    ir_value_t *result = ir_calloc(ir, sizeof(ir_value_t));
    result->type = result_type;
    result->def = instr;
    result->reg = NO_REGISTER;
    instr->result = result;
  }

  list_add_after_entry(&ir->instrs, ir->current_instr, it, instr);

  ir->current_instr = instr;

  return instr;
}

void ir_remove_instr(ir_t *ir, ir_instr_t *instr) {
  // remove arguments from the use lists of their values
  for (int i = 0; i < MAX_INSTR_ARGS; i++) {
    ir_value_t *value = instr->arg[i];

    if (value) {
      ir_remove_use(value, &instr->used[i]);
    }
  }

  list_remove(&ir->instrs, &instr->it);
}

ir_value_t *ir_alloc_i8(ir_t *ir, int8_t c) {
  ir_value_t *v = ir_calloc(ir, sizeof(ir_value_t));
  v->type = VALUE_I8;
  v->i8 = c;
  v->reg = NO_REGISTER;
  return v;
}

ir_value_t *ir_alloc_i16(ir_t *ir, int16_t c) {
  ir_value_t *v = ir_calloc(ir, sizeof(ir_value_t));
  v->type = VALUE_I16;
  v->i16 = c;
  v->reg = NO_REGISTER;
  return v;
}

ir_value_t *ir_alloc_i32(ir_t *ir, int32_t c) {
  ir_value_t *v = ir_calloc(ir, sizeof(ir_value_t));
  v->type = VALUE_I32;
  v->i32 = c;
  v->reg = NO_REGISTER;
  return v;
}

ir_value_t *ir_alloc_i64(ir_t *ir, int64_t c) {
  ir_value_t *v = ir_calloc(ir, sizeof(ir_value_t));
  v->type = VALUE_I64;
  v->i64 = c;
  v->reg = NO_REGISTER;
  return v;
}

ir_value_t *ir_alloc_f32(ir_t *ir, float c) {
  ir_value_t *v = ir_calloc(ir, sizeof(ir_value_t));
  v->type = VALUE_F32;
  v->f32 = c;
  v->reg = NO_REGISTER;
  return v;
}

ir_value_t *ir_alloc_f64(ir_t *ir, double c) {
  ir_value_t *v = ir_calloc(ir, sizeof(ir_value_t));
  v->type = VALUE_F64;
  v->f64 = c;
  v->reg = NO_REGISTER;
  return v;
}

ir_local_t *ir_alloc_local(ir_t *ir, ir_type_t type) {
  // align local to natural size
  int type_size = ir_type_size(type);
  ir->locals_size = align_up(ir->locals_size, type_size);

  ir_local_t *l = ir_calloc(ir, sizeof(ir_local_t));
  l->type = type;
  l->offset = ir_alloc_i32(ir, ir->locals_size);
  list_add(&ir->locals, &l->it);

  ir->locals_size += type_size;

  return l;
}

void ir_set_arg(ir_t *ir, ir_instr_t *instr, int n, ir_value_t *v) {
  ir_replace_use(&instr->used[n], v);
}

void ir_set_arg0(ir_t *ir, ir_instr_t *instr, ir_value_t *v) {
  ir_set_arg(ir, instr, 0, v);
}

void ir_set_arg1(ir_t *ir, ir_instr_t *instr, ir_value_t *v) {
  ir_set_arg(ir, instr, 1, v);
}

void ir_set_arg2(ir_t *ir, ir_instr_t *instr, ir_value_t *v) {
  ir_set_arg(ir, instr, 2, v);
}

void ir_replace_use(ir_use_t *use, ir_value_t *other) {
  if (*use->parg) {
    ir_remove_use(*use->parg, use);
  }

  *use->parg = other;

  if (*use->parg) {
    ir_add_use(*use->parg, use);
  }
}

// replace all uses of v with other
void ir_replace_uses(ir_value_t *v, ir_value_t *other) {
  CHECK_NE(v, other);

  list_for_each_entry_safe(use, &v->uses, ir_use_t, it) {
    ir_replace_use(use, other);
  }
}

bool ir_is_constant(const ir_value_t *v) {
  return !v->def;
}

uint64_t ir_zext_constant(const ir_value_t *v) {
  switch (v->type) {
    case VALUE_I8:
      return (uint8_t)v->i8;
    case VALUE_I16:
      return (uint16_t)v->i16;
    case VALUE_I32:
      return (uint32_t)v->i32;
    case VALUE_I64:
      return (uint64_t)v->i64;
    default:
      LOG_FATAL("Unexpected value type");
      break;
  }
}

ir_value_t *ir_load_host(ir_t *ir, ir_value_t *addr, ir_type_t type) {
  CHECK_EQ(VALUE_I64, addr->type);

  ir_instr_t *instr = ir_append_instr(ir, OP_LOAD_HOST, type);
  ir_set_arg0(ir, instr, addr);
  return instr->result;
}

void ir_store_host(ir_t *ir, ir_value_t *addr, ir_value_t *v) {
  CHECK_EQ(VALUE_I64, addr->type);

  ir_instr_t *instr = ir_append_instr(ir, OP_STORE_HOST, VALUE_V);
  ir_set_arg0(ir, instr, addr);
  ir_set_arg1(ir, instr, v);
}

ir_value_t *ir_load_fast(ir_t *ir, ir_value_t *addr, ir_type_t type) {
  CHECK_EQ(VALUE_I32, addr->type);

  ir_instr_t *instr = ir_append_instr(ir, OP_LOAD_FAST, type);
  ir_set_arg0(ir, instr, addr);
  return instr->result;
}

void ir_store_fast(ir_t *ir, ir_value_t *addr, ir_value_t *v) {
  CHECK_EQ(VALUE_I32, addr->type);

  ir_instr_t *instr = ir_append_instr(ir, OP_STORE_FAST, VALUE_V);
  ir_set_arg0(ir, instr, addr);
  ir_set_arg1(ir, instr, v);
}

ir_value_t *ir_load_slow(ir_t *ir, ir_value_t *addr, ir_type_t type) {
  CHECK_EQ(VALUE_I32, addr->type);

  ir_instr_t *instr = ir_append_instr(ir, OP_LOAD_SLOW, type);
  ir_set_arg0(ir, instr, addr);
  return instr->result;
}

void ir_store_slow(ir_t *ir, ir_value_t *addr, ir_value_t *v) {
  CHECK_EQ(VALUE_I32, addr->type);

  ir_instr_t *instr = ir_append_instr(ir, OP_STORE_SLOW, VALUE_V);
  ir_set_arg0(ir, instr, addr);
  ir_set_arg1(ir, instr, v);
}

ir_value_t *ir_load_context(ir_t *ir, size_t offset, ir_type_t type) {
  ir_instr_t *instr = ir_append_instr(ir, OP_LOAD_CONTEXT, type);
  ir_set_arg0(ir, instr, ir_alloc_i32(ir, offset));
  return instr->result;
}

void ir_store_context(ir_t *ir, size_t offset, ir_value_t *v) {
  ir_instr_t *instr = ir_append_instr(ir, OP_STORE_CONTEXT, VALUE_V);
  ir_set_arg0(ir, instr, ir_alloc_i32(ir, offset));
  ir_set_arg1(ir, instr, v);
}

ir_value_t *ir_load_local(ir_t *ir, ir_local_t *local) {
  ir_instr_t *instr = ir_append_instr(ir, OP_LOAD_LOCAL, local->type);
  ir_set_arg0(ir, instr, local->offset);
  return instr->result;
}

void ir_store_local(ir_t *ir, ir_local_t *local, ir_value_t *v) {
  ir_instr_t *instr = ir_append_instr(ir, OP_STORE_LOCAL, VALUE_V);
  ir_set_arg0(ir, instr, local->offset);
  ir_set_arg1(ir, instr, v);
}

ir_value_t *ir_ftoi(ir_t *ir, ir_value_t *v, ir_type_t dest_type) {
  CHECK(ir_is_float(v->type) && is_is_int(dest_type));

  ir_instr_t *instr = ir_append_instr(ir, OP_FTOI, dest_type);
  ir_set_arg0(ir, instr, v);
  return instr->result;
}

ir_value_t *ir_itof(ir_t *ir, ir_value_t *v, ir_type_t dest_type) {
  CHECK(is_is_int(v->type) && ir_is_float(dest_type));

  ir_instr_t *instr = ir_append_instr(ir, OP_ITOF, dest_type);
  ir_set_arg0(ir, instr, v);
  return instr->result;
}

ir_value_t *ir_sext(ir_t *ir, ir_value_t *v, ir_type_t dest_type) {
  CHECK(is_is_int(v->type) && is_is_int(dest_type));

  ir_instr_t *instr = ir_append_instr(ir, OP_SEXT, dest_type);
  ir_set_arg0(ir, instr, v);
  return instr->result;
}

ir_value_t *ir_zext(ir_t *ir, ir_value_t *v, ir_type_t dest_type) {
  CHECK(is_is_int(v->type) && is_is_int(dest_type));

  ir_instr_t *instr = ir_append_instr(ir, OP_ZEXT, dest_type);
  ir_set_arg0(ir, instr, v);
  return instr->result;
}

ir_value_t *ir_trunc(ir_t *ir, ir_value_t *v, ir_type_t dest_type) {
  CHECK(is_is_int(v->type) && is_is_int(dest_type));

  ir_instr_t *instr = ir_append_instr(ir, OP_TRUNC, dest_type);
  ir_set_arg0(ir, instr, v);
  return instr->result;
}

ir_value_t *ir_fext(ir_t *ir, ir_value_t *v, ir_type_t dest_type) {
  CHECK(v->type == VALUE_F32 && dest_type == VALUE_F64);

  ir_instr_t *instr = ir_append_instr(ir, OP_FEXT, dest_type);
  ir_set_arg0(ir, instr, v);
  return instr->result;
}

ir_value_t *ir_ftrunc(ir_t *ir, ir_value_t *v, ir_type_t dest_type) {
  CHECK(v->type == VALUE_F64 && dest_type == VALUE_F32);

  ir_instr_t *instr = ir_append_instr(ir, OP_FTRUNC, dest_type);
  ir_set_arg0(ir, instr, v);
  return instr->result;
}

ir_value_t *ir_select(ir_t *ir, ir_value_t *cond, ir_value_t *t,
                      ir_value_t *f) {
  CHECK(is_is_int(cond->type) && is_is_int(t->type) && t->type == f->type);

  ir_instr_t *instr = ir_append_instr(ir, OP_SELECT, t->type);
  ir_set_arg0(ir, instr, t);
  ir_set_arg1(ir, instr, f);
  ir_set_arg2(ir, instr, cond);
  return instr->result;
}

static ir_value_t *ir_cmp(ir_t *ir, ir_value_t *a, ir_value_t *b,
                          ir_cmp_t type) {
  CHECK(is_is_int(a->type) && a->type == b->type);

  ir_instr_t *instr = ir_append_instr(ir, OP_CMP, VALUE_I8);
  ir_set_arg0(ir, instr, a);
  ir_set_arg1(ir, instr, b);
  ir_set_arg2(ir, instr, ir_alloc_i32(ir, type));
  return instr->result;
}

ir_value_t *ir_cmp_eq(ir_t *ir, ir_value_t *a, ir_value_t *b) {
  return ir_cmp(ir, a, b, CMP_EQ);
}

ir_value_t *ir_cmp_ne(ir_t *ir, ir_value_t *a, ir_value_t *b) {
  return ir_cmp(ir, a, b, CMP_NE);
}

ir_value_t *ir_cmp_sge(ir_t *ir, ir_value_t *a, ir_value_t *b) {
  return ir_cmp(ir, a, b, CMP_SGE);
}

ir_value_t *ir_cmp_sgt(ir_t *ir, ir_value_t *a, ir_value_t *b) {
  return ir_cmp(ir, a, b, CMP_SGT);
}

ir_value_t *ir_cmp_uge(ir_t *ir, ir_value_t *a, ir_value_t *b) {
  return ir_cmp(ir, a, b, CMP_UGE);
}

ir_value_t *ir_cmp_ugt(ir_t *ir, ir_value_t *a, ir_value_t *b) {
  return ir_cmp(ir, a, b, CMP_UGT);
}

ir_value_t *ir_cmp_sle(ir_t *ir, ir_value_t *a, ir_value_t *b) {
  return ir_cmp(ir, a, b, CMP_SLE);
}

ir_value_t *ir_cmp_slt(ir_t *ir, ir_value_t *a, ir_value_t *b) {
  return ir_cmp(ir, a, b, CMP_SLT);
}

ir_value_t *ir_cmp_ule(ir_t *ir, ir_value_t *a, ir_value_t *b) {
  return ir_cmp(ir, a, b, CMP_ULE);
}

ir_value_t *ir_cmp_ult(ir_t *ir, ir_value_t *a, ir_value_t *b) {
  return ir_cmp(ir, a, b, CMP_ULT);
}

static ir_value_t *ir_fcmp(ir_t *ir, ir_value_t *a, ir_value_t *b,
                           ir_cmp_t type) {
  CHECK(ir_is_float(a->type) && a->type == b->type);

  ir_instr_t *instr = ir_append_instr(ir, OP_FCMP, VALUE_I8);
  ir_set_arg0(ir, instr, a);
  ir_set_arg1(ir, instr, b);
  ir_set_arg2(ir, instr, ir_alloc_i32(ir, type));
  return instr->result;
}

ir_value_t *ir_fcmp_eq(ir_t *ir, ir_value_t *a, ir_value_t *b) {
  return ir_fcmp(ir, a, b, CMP_EQ);
}

ir_value_t *ir_fcmp_ne(ir_t *ir, ir_value_t *a, ir_value_t *b) {
  return ir_fcmp(ir, a, b, CMP_NE);
}

ir_value_t *ir_fcmp_ge(ir_t *ir, ir_value_t *a, ir_value_t *b) {
  return ir_fcmp(ir, a, b, CMP_SGE);
}

ir_value_t *ir_fcmp_gt(ir_t *ir, ir_value_t *a, ir_value_t *b) {
  return ir_fcmp(ir, a, b, CMP_SGT);
}

ir_value_t *ir_fcmp_le(ir_t *ir, ir_value_t *a, ir_value_t *b) {
  return ir_fcmp(ir, a, b, CMP_SLE);
}

ir_value_t *ir_fcmp_lt(ir_t *ir, ir_value_t *a, ir_value_t *b) {
  return ir_fcmp(ir, a, b, CMP_SLT);
}

ir_value_t *ir_add(ir_t *ir, ir_value_t *a, ir_value_t *b) {
  CHECK(is_is_int(a->type) && a->type == b->type);

  ir_instr_t *instr = ir_append_instr(ir, OP_ADD, a->type);
  ir_set_arg0(ir, instr, a);
  ir_set_arg1(ir, instr, b);
  return instr->result;
}

ir_value_t *ir_sub(ir_t *ir, ir_value_t *a, ir_value_t *b) {
  CHECK(is_is_int(a->type) && a->type == b->type);

  ir_instr_t *instr = ir_append_instr(ir, OP_SUB, a->type);
  ir_set_arg0(ir, instr, a);
  ir_set_arg1(ir, instr, b);
  return instr->result;
}

ir_value_t *ir_smul(ir_t *ir, ir_value_t *a, ir_value_t *b) {
  CHECK(is_is_int(a->type) && a->type == b->type);

  ir_instr_t *instr = ir_append_instr(ir, OP_SMUL, a->type);
  ir_set_arg0(ir, instr, a);
  ir_set_arg1(ir, instr, b);
  return instr->result;
}

ir_value_t *ir_umul(ir_t *ir, ir_value_t *a, ir_value_t *b) {
  CHECK(is_is_int(a->type) && a->type == b->type);

  CHECK(is_is_int(a->type));
  ir_instr_t *instr = ir_append_instr(ir, OP_UMUL, a->type);
  ir_set_arg0(ir, instr, a);
  ir_set_arg1(ir, instr, b);
  return instr->result;
}

ir_value_t *ir_div(ir_t *ir, ir_value_t *a, ir_value_t *b) {
  CHECK(is_is_int(a->type) && a->type == b->type);

  ir_instr_t *instr = ir_append_instr(ir, OP_DIV, a->type);
  ir_set_arg0(ir, instr, a);
  ir_set_arg1(ir, instr, b);
  return instr->result;
}

ir_value_t *ir_neg(ir_t *ir, ir_value_t *a) {
  CHECK(is_is_int(a->type));

  ir_instr_t *instr = ir_append_instr(ir, OP_NEG, a->type);
  ir_set_arg0(ir, instr, a);
  return instr->result;
}

ir_value_t *ir_abs(ir_t *ir, ir_value_t *a) {
  CHECK(is_is_int(a->type));

  ir_instr_t *instr = ir_append_instr(ir, OP_ABS, a->type);
  ir_set_arg0(ir, instr, a);
  return instr->result;
}

ir_value_t *ir_fadd(ir_t *ir, ir_value_t *a, ir_value_t *b) {
  CHECK(ir_is_float(a->type) && a->type == b->type);

  ir_instr_t *instr = ir_append_instr(ir, OP_FADD, a->type);
  ir_set_arg0(ir, instr, a);
  ir_set_arg1(ir, instr, b);
  return instr->result;
}

ir_value_t *ir_fsub(ir_t *ir, ir_value_t *a, ir_value_t *b) {
  CHECK(ir_is_float(a->type) && a->type == b->type);

  ir_instr_t *instr = ir_append_instr(ir, OP_FSUB, a->type);
  ir_set_arg0(ir, instr, a);
  ir_set_arg1(ir, instr, b);
  return instr->result;
}

ir_value_t *ir_fmul(ir_t *ir, ir_value_t *a, ir_value_t *b) {
  CHECK(ir_is_float(a->type) && a->type == b->type);

  ir_instr_t *instr = ir_append_instr(ir, OP_FMUL, a->type);
  ir_set_arg0(ir, instr, a);
  ir_set_arg1(ir, instr, b);
  return instr->result;
}

ir_value_t *ir_fdiv(ir_t *ir, ir_value_t *a, ir_value_t *b) {
  CHECK(ir_is_float(a->type) && a->type == b->type);

  ir_instr_t *instr = ir_append_instr(ir, OP_FDIV, a->type);
  ir_set_arg0(ir, instr, a);
  ir_set_arg1(ir, instr, b);
  return instr->result;
}

ir_value_t *ir_fneg(ir_t *ir, ir_value_t *a) {
  CHECK(ir_is_float(a->type));

  ir_instr_t *instr = ir_append_instr(ir, OP_FNEG, a->type);
  ir_set_arg0(ir, instr, a);
  return instr->result;
}

ir_value_t *ir_fabs(ir_t *ir, ir_value_t *a) {
  CHECK(ir_is_float(a->type));

  ir_instr_t *instr = ir_append_instr(ir, OP_FABS, a->type);
  ir_set_arg0(ir, instr, a);
  return instr->result;
}

ir_value_t *ir_sqrt(ir_t *ir, ir_value_t *a) {
  CHECK(ir_is_float(a->type));

  ir_instr_t *instr = ir_append_instr(ir, OP_SQRT, a->type);
  ir_set_arg0(ir, instr, a);
  return instr->result;
}

ir_value_t *ir_vbroadcast(ir_t *ir, ir_value_t *a) {
  CHECK(a->type == VALUE_F32);

  ir_instr_t *instr = ir_append_instr(ir, OP_VBROADCAST, VALUE_V128);
  ir_set_arg0(ir, instr, a);
  return instr->result;
}

ir_value_t *ir_vadd(ir_t *ir, ir_value_t *a, ir_value_t *b, ir_type_t el_type) {
  CHECK(ir_is_vector(a->type) && ir_is_vector(b->type));
  CHECK_EQ(el_type, VALUE_F32);

  ir_instr_t *instr = ir_append_instr(ir, OP_VADD, a->type);
  ir_set_arg0(ir, instr, a);
  ir_set_arg1(ir, instr, b);
  return instr->result;
}

ir_value_t *ir_vdot(ir_t *ir, ir_value_t *a, ir_value_t *b, ir_type_t el_type) {
  CHECK(ir_is_vector(a->type) && ir_is_vector(b->type));
  CHECK_EQ(el_type, VALUE_F32);

  ir_instr_t *instr = ir_append_instr(ir, OP_VDOT, el_type);
  ir_set_arg0(ir, instr, a);
  ir_set_arg1(ir, instr, b);
  return instr->result;
}

ir_value_t *ir_vmul(ir_t *ir, ir_value_t *a, ir_value_t *b, ir_type_t el_type) {
  CHECK(ir_is_vector(a->type) && ir_is_vector(b->type));
  CHECK_EQ(el_type, VALUE_F32);

  ir_instr_t *instr = ir_append_instr(ir, OP_VMUL, a->type);
  ir_set_arg0(ir, instr, a);
  ir_set_arg1(ir, instr, b);
  return instr->result;
}

ir_value_t *ir_and(ir_t *ir, ir_value_t *a, ir_value_t *b) {
  CHECK(is_is_int(a->type) && a->type == b->type);

  ir_instr_t *instr = ir_append_instr(ir, OP_AND, a->type);
  ir_set_arg0(ir, instr, a);
  ir_set_arg1(ir, instr, b);
  return instr->result;
}

ir_value_t *ir_or(ir_t *ir, ir_value_t *a, ir_value_t *b) {
  CHECK(is_is_int(a->type) && a->type == b->type);

  ir_instr_t *instr = ir_append_instr(ir, OP_OR, a->type);
  ir_set_arg0(ir, instr, a);
  ir_set_arg1(ir, instr, b);
  return instr->result;
}

ir_value_t *ir_xor(ir_t *ir, ir_value_t *a, ir_value_t *b) {
  CHECK(is_is_int(a->type) && a->type == b->type);

  ir_instr_t *instr = ir_append_instr(ir, OP_XOR, a->type);
  ir_set_arg0(ir, instr, a);
  ir_set_arg1(ir, instr, b);
  return instr->result;
}

ir_value_t *ir_not(ir_t *ir, ir_value_t *a) {
  CHECK(is_is_int(a->type));

  ir_instr_t *instr = ir_append_instr(ir, OP_NOT, a->type);
  ir_set_arg0(ir, instr, a);
  return instr->result;
}

ir_value_t *ir_shl(ir_t *ir, ir_value_t *a, ir_value_t *n) {
  CHECK(is_is_int(a->type) && n->type == VALUE_I32);

  ir_instr_t *instr = ir_append_instr(ir, OP_SHL, a->type);
  ir_set_arg0(ir, instr, a);
  ir_set_arg1(ir, instr, n);
  return instr->result;
}

ir_value_t *ir_shli(ir_t *ir, ir_value_t *a, int n) {
  return ir_shl(ir, a, ir_alloc_i32(ir, n));
}

ir_value_t *ir_ashr(ir_t *ir, ir_value_t *a, ir_value_t *n) {
  CHECK(is_is_int(a->type) && n->type == VALUE_I32);

  ir_instr_t *instr = ir_append_instr(ir, OP_ASHR, a->type);
  ir_set_arg0(ir, instr, a);
  ir_set_arg1(ir, instr, n);
  return instr->result;
}

ir_value_t *ir_ashri(ir_t *ir, ir_value_t *a, int n) {
  return ir_ashr(ir, a, ir_alloc_i32(ir, n));
}

ir_value_t *ir_lshr(ir_t *ir, ir_value_t *a, ir_value_t *n) {
  CHECK(is_is_int(a->type) && n->type == VALUE_I32);

  ir_instr_t *instr = ir_append_instr(ir, OP_LSHR, a->type);
  ir_set_arg0(ir, instr, a);
  ir_set_arg1(ir, instr, n);
  return instr->result;
}

ir_value_t *ir_lshri(ir_t *ir, ir_value_t *a, int n) {
  return ir_lshr(ir, a, ir_alloc_i32(ir, n));
}

ir_value_t *ir_ashd(ir_t *ir, ir_value_t *a, ir_value_t *n) {
  CHECK(a->type == VALUE_I32 && n->type == VALUE_I32);

  ir_instr_t *instr = ir_append_instr(ir, OP_ASHD, a->type);
  ir_set_arg0(ir, instr, a);
  ir_set_arg1(ir, instr, n);
  return instr->result;
}

ir_value_t *ir_lshd(ir_t *ir, ir_value_t *a, ir_value_t *n) {
  CHECK(a->type == VALUE_I32 && n->type == VALUE_I32);

  ir_instr_t *instr = ir_append_instr(ir, OP_LSHD, a->type);
  ir_set_arg0(ir, instr, a);
  ir_set_arg1(ir, instr, n);
  return instr->result;
}

void ir_branch(ir_t *ir, ir_value_t *dest) {
  ir_instr_t *instr = ir_append_instr(ir, OP_BRANCH, VALUE_V);
  ir_set_arg0(ir, instr, dest);
}

void ir_branch_cond(ir_t *ir, ir_value_t *cond, ir_value_t *true_addr,
                    ir_value_t *false_addr) {
  ir_instr_t *instr = ir_append_instr(ir, OP_BRANCH_COND, VALUE_V);
  ir_set_arg0(ir, instr, cond);
  ir_set_arg1(ir, instr, true_addr);
  ir_set_arg2(ir, instr, false_addr);
}

void ir_call_external_1(ir_t *ir, ir_value_t *addr) {
  CHECK_EQ(addr->type, VALUE_I64);

  ir_instr_t *instr = ir_append_instr(ir, OP_CALL_EXTERNAL, VALUE_V);
  ir_set_arg0(ir, instr, addr);
}

void ir_call_external_2(ir_t *ir, ir_value_t *addr, ir_value_t *arg0) {
  CHECK_EQ(addr->type, VALUE_I64);
  CHECK_EQ(arg0->type, VALUE_I64);

  ir_instr_t *instr = ir_append_instr(ir, OP_CALL_EXTERNAL, VALUE_V);
  ir_set_arg0(ir, instr, addr);
  ir_set_arg1(ir, instr, arg0);
}
