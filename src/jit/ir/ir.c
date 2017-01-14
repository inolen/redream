#include <stdarg.h>
#include <stdio.h>
#include "jit/ir/ir.h"
#include "core/math.h"

const char *ir_op_names[NUM_OPS] = {
#define IR_OP(name) #name,
#include "jit/ir/ir_ops.inc"
};

static void *ir_calloc(struct ir *ir, int size) {
  CHECK_LE(ir->used + size, ir->capacity);
  uint8_t *ptr = ir->buffer + ir->used;
  memset(ptr, 0, size);
  ir->used += size;
  return ptr;
}

static struct ir_instr *ir_alloc_instr(struct ir *ir, enum ir_op op) {
  struct ir_instr *instr = ir_calloc(ir, sizeof(struct ir_instr));

  instr->op = op;

  /* initialize use links */
  for (int i = 0; i < MAX_INSTR_ARGS; i++) {
    struct ir_use *use = &instr->used[i];
    use->instr = instr;
    use->parg = &instr->arg[i];
  }

  return instr;
}

static void ir_add_use(struct ir_value *v, struct ir_use *use) {
  list_add(&v->uses, &use->it);
}

static void ir_remove_use(struct ir_value *v, struct ir_use *use) {
  list_remove(&v->uses, &use->it);
}

struct ir_instr *ir_append_instr(struct ir *ir, enum ir_op op,
                                 enum ir_type result_type) {
  struct ir_instr *instr = ir_alloc_instr(ir, op);

  /* allocate result if needed */
  if (result_type != VALUE_V) {
    struct ir_value *result = ir_calloc(ir, sizeof(struct ir_value));
    result->type = result_type;
    result->def = instr;
    result->reg = NO_REGISTER;
    instr->result = result;
  }

  list_add_after_entry(&ir->instrs, ir->current_instr, it, instr);

  ir->current_instr = instr;

  return instr;
}

void ir_remove_instr(struct ir *ir, struct ir_instr *instr) {
  /* remove arguments from the use lists of their values */
  for (int i = 0; i < MAX_INSTR_ARGS; i++) {
    struct ir_value *value = instr->arg[i];

    if (value) {
      ir_remove_use(value, &instr->used[i]);
    }
  }

  list_remove(&ir->instrs, &instr->it);
}

struct ir_value *ir_alloc_int(struct ir *ir, int64_t c, enum ir_type type) {
  struct ir_value *v = ir_calloc(ir, sizeof(struct ir_value));
  v->type = type;
  switch (type) {
    case VALUE_I8:
      v->i8 = (int8_t)c;
      break;
    case VALUE_I16:
      v->i16 = (int16_t)c;
      break;
    case VALUE_I32:
      v->i32 = (int32_t)c;
      break;
    case VALUE_I64:
      v->i64 = c;
      break;
    default:
      LOG_FATAL("Unexpected value type");
      break;
  }
  v->reg = NO_REGISTER;
  return v;
}

struct ir_value *ir_alloc_i8(struct ir *ir, int8_t c) {
  struct ir_value *v = ir_calloc(ir, sizeof(struct ir_value));
  v->type = VALUE_I8;
  v->i8 = c;
  v->reg = NO_REGISTER;
  return v;
}

struct ir_value *ir_alloc_i16(struct ir *ir, int16_t c) {
  struct ir_value *v = ir_calloc(ir, sizeof(struct ir_value));
  v->type = VALUE_I16;
  v->i16 = c;
  v->reg = NO_REGISTER;
  return v;
}

struct ir_value *ir_alloc_i32(struct ir *ir, int32_t c) {
  struct ir_value *v = ir_calloc(ir, sizeof(struct ir_value));
  v->type = VALUE_I32;
  v->i32 = c;
  v->reg = NO_REGISTER;
  return v;
}

struct ir_value *ir_alloc_i64(struct ir *ir, int64_t c) {
  struct ir_value *v = ir_calloc(ir, sizeof(struct ir_value));
  v->type = VALUE_I64;
  v->i64 = c;
  v->reg = NO_REGISTER;
  return v;
}

struct ir_value *ir_alloc_f32(struct ir *ir, float c) {
  struct ir_value *v = ir_calloc(ir, sizeof(struct ir_value));
  v->type = VALUE_F32;
  v->f32 = c;
  v->reg = NO_REGISTER;
  return v;
}

struct ir_value *ir_alloc_f64(struct ir *ir, double c) {
  struct ir_value *v = ir_calloc(ir, sizeof(struct ir_value));
  v->type = VALUE_F64;
  v->f64 = c;
  v->reg = NO_REGISTER;
  return v;
}

struct ir_value *ir_alloc_str(struct ir *ir, const char *format, ...) {
  struct ir_value *v = ir_calloc(ir, sizeof(struct ir_value));

  v->type = VALUE_STRING;
  v->reg = NO_REGISTER;

  /* format the string */
  va_list args;

  va_start(args, format);
  int name_len = vsnprintf(0, 0, format, args);
  int name_size = name_len + 1;
  v->str = ir_calloc(ir, name_size);
  va_end(args);

  va_start(args, format);
  vsnprintf(v->str, name_size, format, args);
  va_end(args);

  return v;
}

struct ir_value *ir_alloc_ptr(struct ir *ir, void *c) {
  return ir_alloc_i64(ir, (uint64_t)c);
}

struct ir_local *ir_alloc_local(struct ir *ir, enum ir_type type) {
  /* align local to natural size */
  int type_size = ir_type_size(type);
  ir->locals_size = align_up(ir->locals_size, type_size);

  struct ir_local *l = ir_calloc(ir, sizeof(struct ir_local));
  l->type = type;
  l->offset = ir_alloc_i32(ir, ir->locals_size);

  ir->locals_size += type_size;

  return l;
}

struct ir_local *ir_reuse_local(struct ir *ir, struct ir_value *offset,
                                enum ir_type type) {
  struct ir_local *l = ir_calloc(ir, sizeof(struct ir_local));
  l->type = type;
  l->offset = offset;

  return l;
}

void ir_set_arg(struct ir *ir, struct ir_instr *instr, int n,
                struct ir_value *v) {
  ir_replace_use(&instr->used[n], v);
}

void ir_set_arg0(struct ir *ir, struct ir_instr *instr, struct ir_value *v) {
  ir_set_arg(ir, instr, 0, v);
}

void ir_set_arg1(struct ir *ir, struct ir_instr *instr, struct ir_value *v) {
  ir_set_arg(ir, instr, 1, v);
}

void ir_set_arg2(struct ir *ir, struct ir_instr *instr, struct ir_value *v) {
  ir_set_arg(ir, instr, 2, v);
}

void ir_replace_use(struct ir_use *use, struct ir_value *other) {
  if (*use->parg) {
    ir_remove_use(*use->parg, use);
  }

  *use->parg = other;

  if (*use->parg) {
    ir_add_use(*use->parg, use);
  }
}

void ir_replace_uses(struct ir_value *v, struct ir_value *other) {
  /* replace all uses of v with other */
  CHECK_NE(v, other);

  list_for_each_entry_safe(use, &v->uses, struct ir_use, it) {
    ir_replace_use(use, other);
  }
}

uint64_t ir_zext_constant(const struct ir_value *v) {
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

struct ir_value *ir_load(struct ir *ir, struct ir_value *addr,
                         enum ir_type type) {
  CHECK_EQ(VALUE_I64, addr->type);

  struct ir_instr *instr = ir_append_instr(ir, OP_LOAD, type);
  ir_set_arg0(ir, instr, addr);
  return instr->result;
}

void ir_store(struct ir *ir, struct ir_value *addr, struct ir_value *v) {
  CHECK_EQ(VALUE_I64, addr->type);

  struct ir_instr *instr = ir_append_instr(ir, OP_STORE, VALUE_V);
  ir_set_arg0(ir, instr, addr);
  ir_set_arg1(ir, instr, v);
}

struct ir_value *ir_load_fast(struct ir *ir, struct ir_value *addr,
                              enum ir_type type) {
  CHECK_EQ(VALUE_I32, addr->type);

  struct ir_instr *instr = ir_append_instr(ir, OP_LOAD_FAST, type);
  ir_set_arg0(ir, instr, addr);
  return instr->result;
}

void ir_store_fast(struct ir *ir, struct ir_value *addr, struct ir_value *v) {
  CHECK_EQ(VALUE_I32, addr->type);

  struct ir_instr *instr = ir_append_instr(ir, OP_STORE_FAST, VALUE_V);
  ir_set_arg0(ir, instr, addr);
  ir_set_arg1(ir, instr, v);
}

struct ir_value *ir_load_slow(struct ir *ir, struct ir_value *addr,
                              enum ir_type type) {
  CHECK_EQ(VALUE_I32, addr->type);

  struct ir_instr *instr = ir_append_instr(ir, OP_LOAD_SLOW, type);
  ir_set_arg0(ir, instr, addr);
  return instr->result;
}

void ir_store_slow(struct ir *ir, struct ir_value *addr, struct ir_value *v) {
  CHECK_EQ(VALUE_I32, addr->type);

  struct ir_instr *instr = ir_append_instr(ir, OP_STORE_SLOW, VALUE_V);
  ir_set_arg0(ir, instr, addr);
  ir_set_arg1(ir, instr, v);
}

struct ir_value *ir_load_context(struct ir *ir, size_t offset,
                                 enum ir_type type) {
  struct ir_instr *instr = ir_append_instr(ir, OP_LOAD_CONTEXT, type);
  ir_set_arg0(ir, instr, ir_alloc_i32(ir, (int32_t)offset));
  return instr->result;
}

void ir_store_context(struct ir *ir, size_t offset, struct ir_value *v) {
  struct ir_instr *instr = ir_append_instr(ir, OP_STORE_CONTEXT, VALUE_V);
  ir_set_arg0(ir, instr, ir_alloc_i32(ir, (int32_t)offset));
  ir_set_arg1(ir, instr, v);
}

struct ir_value *ir_load_local(struct ir *ir, struct ir_local *local) {
  struct ir_instr *instr = ir_append_instr(ir, OP_LOAD_LOCAL, local->type);
  ir_set_arg0(ir, instr, local->offset);
  return instr->result;
}

void ir_store_local(struct ir *ir, struct ir_local *local, struct ir_value *v) {
  struct ir_instr *instr = ir_append_instr(ir, OP_STORE_LOCAL, VALUE_V);
  ir_set_arg0(ir, instr, local->offset);
  ir_set_arg1(ir, instr, v);
}

struct ir_value *ir_ftoi(struct ir *ir, struct ir_value *v,
                         enum ir_type dest_type) {
  CHECK(ir_is_float(v->type) && ir_is_int(dest_type));

  struct ir_instr *instr = ir_append_instr(ir, OP_FTOI, dest_type);
  ir_set_arg0(ir, instr, v);
  return instr->result;
}

struct ir_value *ir_itof(struct ir *ir, struct ir_value *v,
                         enum ir_type dest_type) {
  CHECK(ir_is_int(v->type) && ir_is_float(dest_type));

  struct ir_instr *instr = ir_append_instr(ir, OP_ITOF, dest_type);
  ir_set_arg0(ir, instr, v);
  return instr->result;
}

struct ir_value *ir_sext(struct ir *ir, struct ir_value *v,
                         enum ir_type dest_type) {
  CHECK(ir_is_int(v->type) && ir_is_int(dest_type));

  struct ir_instr *instr = ir_append_instr(ir, OP_SEXT, dest_type);
  ir_set_arg0(ir, instr, v);
  return instr->result;
}

struct ir_value *ir_zext(struct ir *ir, struct ir_value *v,
                         enum ir_type dest_type) {
  CHECK(ir_is_int(v->type) && ir_is_int(dest_type));

  struct ir_instr *instr = ir_append_instr(ir, OP_ZEXT, dest_type);
  ir_set_arg0(ir, instr, v);
  return instr->result;
}

struct ir_value *ir_trunc(struct ir *ir, struct ir_value *v,
                          enum ir_type dest_type) {
  CHECK(ir_is_int(v->type) && ir_is_int(dest_type));

  struct ir_instr *instr = ir_append_instr(ir, OP_TRUNC, dest_type);
  ir_set_arg0(ir, instr, v);
  return instr->result;
}

struct ir_value *ir_fext(struct ir *ir, struct ir_value *v,
                         enum ir_type dest_type) {
  CHECK(v->type == VALUE_F32 && dest_type == VALUE_F64);

  struct ir_instr *instr = ir_append_instr(ir, OP_FEXT, dest_type);
  ir_set_arg0(ir, instr, v);
  return instr->result;
}

struct ir_value *ir_ftrunc(struct ir *ir, struct ir_value *v,
                           enum ir_type dest_type) {
  CHECK(v->type == VALUE_F64 && dest_type == VALUE_F32);

  struct ir_instr *instr = ir_append_instr(ir, OP_FTRUNC, dest_type);
  ir_set_arg0(ir, instr, v);
  return instr->result;
}

struct ir_value *ir_select(struct ir *ir, struct ir_value *cond,
                           struct ir_value *t, struct ir_value *f) {
  CHECK(ir_is_int(cond->type) && ir_is_int(t->type) && t->type == f->type);

  struct ir_instr *instr = ir_append_instr(ir, OP_SELECT, t->type);
  ir_set_arg0(ir, instr, t);
  ir_set_arg1(ir, instr, f);
  ir_set_arg2(ir, instr, cond);
  return instr->result;
}

static struct ir_value *ir_cmp(struct ir *ir, struct ir_value *a,
                               struct ir_value *b, enum ir_cmp type) {
  CHECK(ir_is_int(a->type) && a->type == b->type);

  struct ir_instr *instr = ir_append_instr(ir, OP_CMP, VALUE_I8);
  ir_set_arg0(ir, instr, a);
  ir_set_arg1(ir, instr, b);
  ir_set_arg2(ir, instr, ir_alloc_i32(ir, type));
  return instr->result;
}

struct ir_value *ir_cmp_eq(struct ir *ir, struct ir_value *a,
                           struct ir_value *b) {
  return ir_cmp(ir, a, b, CMP_EQ);
}

struct ir_value *ir_cmp_ne(struct ir *ir, struct ir_value *a,
                           struct ir_value *b) {
  return ir_cmp(ir, a, b, CMP_NE);
}

struct ir_value *ir_cmp_sge(struct ir *ir, struct ir_value *a,
                            struct ir_value *b) {
  return ir_cmp(ir, a, b, CMP_SGE);
}

struct ir_value *ir_cmp_sgt(struct ir *ir, struct ir_value *a,
                            struct ir_value *b) {
  return ir_cmp(ir, a, b, CMP_SGT);
}

struct ir_value *ir_cmp_uge(struct ir *ir, struct ir_value *a,
                            struct ir_value *b) {
  return ir_cmp(ir, a, b, CMP_UGE);
}

struct ir_value *ir_cmp_ugt(struct ir *ir, struct ir_value *a,
                            struct ir_value *b) {
  return ir_cmp(ir, a, b, CMP_UGT);
}

struct ir_value *ir_cmp_sle(struct ir *ir, struct ir_value *a,
                            struct ir_value *b) {
  return ir_cmp(ir, a, b, CMP_SLE);
}

struct ir_value *ir_cmp_slt(struct ir *ir, struct ir_value *a,
                            struct ir_value *b) {
  return ir_cmp(ir, a, b, CMP_SLT);
}

struct ir_value *ir_cmp_ule(struct ir *ir, struct ir_value *a,
                            struct ir_value *b) {
  return ir_cmp(ir, a, b, CMP_ULE);
}

struct ir_value *ir_cmp_ult(struct ir *ir, struct ir_value *a,
                            struct ir_value *b) {
  return ir_cmp(ir, a, b, CMP_ULT);
}

static struct ir_value *ir_fcmp(struct ir *ir, struct ir_value *a,
                                struct ir_value *b, enum ir_cmp type) {
  CHECK(ir_is_float(a->type) && a->type == b->type);

  struct ir_instr *instr = ir_append_instr(ir, OP_FCMP, VALUE_I8);
  ir_set_arg0(ir, instr, a);
  ir_set_arg1(ir, instr, b);
  ir_set_arg2(ir, instr, ir_alloc_i32(ir, type));
  return instr->result;
}

struct ir_value *ir_fcmp_eq(struct ir *ir, struct ir_value *a,
                            struct ir_value *b) {
  return ir_fcmp(ir, a, b, CMP_EQ);
}

struct ir_value *ir_fcmp_ne(struct ir *ir, struct ir_value *a,
                            struct ir_value *b) {
  return ir_fcmp(ir, a, b, CMP_NE);
}

struct ir_value *ir_fcmp_ge(struct ir *ir, struct ir_value *a,
                            struct ir_value *b) {
  return ir_fcmp(ir, a, b, CMP_SGE);
}

struct ir_value *ir_fcmp_gt(struct ir *ir, struct ir_value *a,
                            struct ir_value *b) {
  return ir_fcmp(ir, a, b, CMP_SGT);
}

struct ir_value *ir_fcmp_le(struct ir *ir, struct ir_value *a,
                            struct ir_value *b) {
  return ir_fcmp(ir, a, b, CMP_SLE);
}

struct ir_value *ir_fcmp_lt(struct ir *ir, struct ir_value *a,
                            struct ir_value *b) {
  return ir_fcmp(ir, a, b, CMP_SLT);
}

struct ir_value *ir_add(struct ir *ir, struct ir_value *a, struct ir_value *b) {
  CHECK(ir_is_int(a->type) && a->type == b->type);

  struct ir_instr *instr = ir_append_instr(ir, OP_ADD, a->type);
  ir_set_arg0(ir, instr, a);
  ir_set_arg1(ir, instr, b);
  return instr->result;
}

struct ir_value *ir_sub(struct ir *ir, struct ir_value *a, struct ir_value *b) {
  CHECK(ir_is_int(a->type) && a->type == b->type);

  struct ir_instr *instr = ir_append_instr(ir, OP_SUB, a->type);
  ir_set_arg0(ir, instr, a);
  ir_set_arg1(ir, instr, b);
  return instr->result;
}

struct ir_value *ir_smul(struct ir *ir, struct ir_value *a,
                         struct ir_value *b) {
  CHECK(ir_is_int(a->type) && a->type == b->type);

  struct ir_instr *instr = ir_append_instr(ir, OP_SMUL, a->type);
  ir_set_arg0(ir, instr, a);
  ir_set_arg1(ir, instr, b);
  return instr->result;
}

struct ir_value *ir_umul(struct ir *ir, struct ir_value *a,
                         struct ir_value *b) {
  CHECK(ir_is_int(a->type) && a->type == b->type);

  CHECK(ir_is_int(a->type));
  struct ir_instr *instr = ir_append_instr(ir, OP_UMUL, a->type);
  ir_set_arg0(ir, instr, a);
  ir_set_arg1(ir, instr, b);
  return instr->result;
}

struct ir_value *ir_div(struct ir *ir, struct ir_value *a, struct ir_value *b) {
  CHECK(ir_is_int(a->type) && a->type == b->type);

  struct ir_instr *instr = ir_append_instr(ir, OP_DIV, a->type);
  ir_set_arg0(ir, instr, a);
  ir_set_arg1(ir, instr, b);
  return instr->result;
}

struct ir_value *ir_neg(struct ir *ir, struct ir_value *a) {
  CHECK(ir_is_int(a->type));

  struct ir_instr *instr = ir_append_instr(ir, OP_NEG, a->type);
  ir_set_arg0(ir, instr, a);
  return instr->result;
}

struct ir_value *ir_abs(struct ir *ir, struct ir_value *a) {
  CHECK(ir_is_int(a->type));

  struct ir_instr *instr = ir_append_instr(ir, OP_ABS, a->type);
  ir_set_arg0(ir, instr, a);
  return instr->result;
}

struct ir_value *ir_fadd(struct ir *ir, struct ir_value *a,
                         struct ir_value *b) {
  CHECK(ir_is_float(a->type) && a->type == b->type);

  struct ir_instr *instr = ir_append_instr(ir, OP_FADD, a->type);
  ir_set_arg0(ir, instr, a);
  ir_set_arg1(ir, instr, b);
  return instr->result;
}

struct ir_value *ir_fsub(struct ir *ir, struct ir_value *a,
                         struct ir_value *b) {
  CHECK(ir_is_float(a->type) && a->type == b->type);

  struct ir_instr *instr = ir_append_instr(ir, OP_FSUB, a->type);
  ir_set_arg0(ir, instr, a);
  ir_set_arg1(ir, instr, b);
  return instr->result;
}

struct ir_value *ir_fmul(struct ir *ir, struct ir_value *a,
                         struct ir_value *b) {
  CHECK(ir_is_float(a->type) && a->type == b->type);

  struct ir_instr *instr = ir_append_instr(ir, OP_FMUL, a->type);
  ir_set_arg0(ir, instr, a);
  ir_set_arg1(ir, instr, b);
  return instr->result;
}

struct ir_value *ir_fdiv(struct ir *ir, struct ir_value *a,
                         struct ir_value *b) {
  CHECK(ir_is_float(a->type) && a->type == b->type);

  struct ir_instr *instr = ir_append_instr(ir, OP_FDIV, a->type);
  ir_set_arg0(ir, instr, a);
  ir_set_arg1(ir, instr, b);
  return instr->result;
}

struct ir_value *ir_fneg(struct ir *ir, struct ir_value *a) {
  CHECK(ir_is_float(a->type));

  struct ir_instr *instr = ir_append_instr(ir, OP_FNEG, a->type);
  ir_set_arg0(ir, instr, a);
  return instr->result;
}

struct ir_value *ir_fabs(struct ir *ir, struct ir_value *a) {
  CHECK(ir_is_float(a->type));

  struct ir_instr *instr = ir_append_instr(ir, OP_FABS, a->type);
  ir_set_arg0(ir, instr, a);
  return instr->result;
}

struct ir_value *ir_sqrt(struct ir *ir, struct ir_value *a) {
  CHECK(ir_is_float(a->type));

  struct ir_instr *instr = ir_append_instr(ir, OP_SQRT, a->type);
  ir_set_arg0(ir, instr, a);
  return instr->result;
}

struct ir_value *ir_vbroadcast(struct ir *ir, struct ir_value *a) {
  CHECK(a->type == VALUE_F32);

  struct ir_instr *instr = ir_append_instr(ir, OP_VBROADCAST, VALUE_V128);
  ir_set_arg0(ir, instr, a);
  return instr->result;
}

struct ir_value *ir_vadd(struct ir *ir, struct ir_value *a, struct ir_value *b,
                         enum ir_type el_type) {
  CHECK(ir_is_vector(a->type) && ir_is_vector(b->type));
  CHECK_EQ(el_type, VALUE_F32);

  struct ir_instr *instr = ir_append_instr(ir, OP_VADD, a->type);
  ir_set_arg0(ir, instr, a);
  ir_set_arg1(ir, instr, b);
  return instr->result;
}

struct ir_value *ir_vdot(struct ir *ir, struct ir_value *a, struct ir_value *b,
                         enum ir_type el_type) {
  CHECK(ir_is_vector(a->type) && ir_is_vector(b->type));
  CHECK_EQ(el_type, VALUE_F32);

  struct ir_instr *instr = ir_append_instr(ir, OP_VDOT, el_type);
  ir_set_arg0(ir, instr, a);
  ir_set_arg1(ir, instr, b);
  return instr->result;
}

struct ir_value *ir_vmul(struct ir *ir, struct ir_value *a, struct ir_value *b,
                         enum ir_type el_type) {
  CHECK(ir_is_vector(a->type) && ir_is_vector(b->type));
  CHECK_EQ(el_type, VALUE_F32);

  struct ir_instr *instr = ir_append_instr(ir, OP_VMUL, a->type);
  ir_set_arg0(ir, instr, a);
  ir_set_arg1(ir, instr, b);
  return instr->result;
}

struct ir_value *ir_and(struct ir *ir, struct ir_value *a, struct ir_value *b) {
  CHECK(ir_is_int(a->type) && a->type == b->type);

  struct ir_instr *instr = ir_append_instr(ir, OP_AND, a->type);
  ir_set_arg0(ir, instr, a);
  ir_set_arg1(ir, instr, b);
  return instr->result;
}

struct ir_value *ir_or(struct ir *ir, struct ir_value *a, struct ir_value *b) {
  CHECK(ir_is_int(a->type) && a->type == b->type);

  struct ir_instr *instr = ir_append_instr(ir, OP_OR, a->type);
  ir_set_arg0(ir, instr, a);
  ir_set_arg1(ir, instr, b);
  return instr->result;
}

struct ir_value *ir_xor(struct ir *ir, struct ir_value *a, struct ir_value *b) {
  CHECK(ir_is_int(a->type) && a->type == b->type);

  struct ir_instr *instr = ir_append_instr(ir, OP_XOR, a->type);
  ir_set_arg0(ir, instr, a);
  ir_set_arg1(ir, instr, b);
  return instr->result;
}

struct ir_value *ir_not(struct ir *ir, struct ir_value *a) {
  CHECK(ir_is_int(a->type));

  struct ir_instr *instr = ir_append_instr(ir, OP_NOT, a->type);
  ir_set_arg0(ir, instr, a);
  return instr->result;
}

struct ir_value *ir_shl(struct ir *ir, struct ir_value *a, struct ir_value *n) {
  CHECK(ir_is_int(a->type) && n->type == VALUE_I32);

  struct ir_instr *instr = ir_append_instr(ir, OP_SHL, a->type);
  ir_set_arg0(ir, instr, a);
  ir_set_arg1(ir, instr, n);
  return instr->result;
}

struct ir_value *ir_shli(struct ir *ir, struct ir_value *a, int n) {
  return ir_shl(ir, a, ir_alloc_i32(ir, n));
}

struct ir_value *ir_ashr(struct ir *ir, struct ir_value *a,
                         struct ir_value *n) {
  CHECK(ir_is_int(a->type) && n->type == VALUE_I32);

  struct ir_instr *instr = ir_append_instr(ir, OP_ASHR, a->type);
  ir_set_arg0(ir, instr, a);
  ir_set_arg1(ir, instr, n);
  return instr->result;
}

struct ir_value *ir_ashri(struct ir *ir, struct ir_value *a, int n) {
  return ir_ashr(ir, a, ir_alloc_i32(ir, n));
}

struct ir_value *ir_lshr(struct ir *ir, struct ir_value *a,
                         struct ir_value *n) {
  CHECK(ir_is_int(a->type) && n->type == VALUE_I32);

  struct ir_instr *instr = ir_append_instr(ir, OP_LSHR, a->type);
  ir_set_arg0(ir, instr, a);
  ir_set_arg1(ir, instr, n);
  return instr->result;
}

struct ir_value *ir_lshri(struct ir *ir, struct ir_value *a, int n) {
  return ir_lshr(ir, a, ir_alloc_i32(ir, n));
}

struct ir_value *ir_ashd(struct ir *ir, struct ir_value *a,
                         struct ir_value *n) {
  CHECK(a->type == VALUE_I32 && n->type == VALUE_I32);

  struct ir_instr *instr = ir_append_instr(ir, OP_ASHD, a->type);
  ir_set_arg0(ir, instr, a);
  ir_set_arg1(ir, instr, n);
  return instr->result;
}

struct ir_value *ir_lshd(struct ir *ir, struct ir_value *a,
                         struct ir_value *n) {
  CHECK(a->type == VALUE_I32 && n->type == VALUE_I32);

  struct ir_instr *instr = ir_append_instr(ir, OP_LSHD, a->type);
  ir_set_arg0(ir, instr, a);
  ir_set_arg1(ir, instr, n);
  return instr->result;
}

void ir_label(struct ir *ir, struct ir_value *lbl) {
  CHECK(lbl->type == VALUE_STRING);

  struct ir_instr *instr = ir_append_instr(ir, OP_LABEL, VALUE_V);
  ir_set_arg0(ir, instr, lbl);
}

void ir_branch(struct ir *ir, struct ir_value *dst) {
  CHECK(dst->type == VALUE_STRING || dst->type == VALUE_I64);

  struct ir_instr *instr = ir_append_instr(ir, OP_BRANCH, VALUE_V);
  ir_set_arg0(ir, instr, dst);
}

void ir_branch_false(struct ir *ir, struct ir_value *dst,
                     struct ir_value *cond) {
  CHECK(dst->type == VALUE_STRING || dst->type == VALUE_I64);

  struct ir_instr *instr = ir_append_instr(ir, OP_BRANCH_FALSE, VALUE_V);
  ir_set_arg0(ir, instr, dst);
  ir_set_arg1(ir, instr, cond);
}

void ir_branch_true(struct ir *ir, struct ir_value *dst,
                    struct ir_value *cond) {
  CHECK(dst->type == VALUE_STRING || dst->type == VALUE_I64);

  struct ir_instr *instr = ir_append_instr(ir, OP_BRANCH_TRUE, VALUE_V);
  ir_set_arg0(ir, instr, dst);
  ir_set_arg1(ir, instr, cond);
}

void ir_call(struct ir *ir, struct ir_value *fn) {
  struct ir_instr *instr = ir_append_instr(ir, OP_CALL, VALUE_V);
  ir_set_arg0(ir, instr, fn);
}

void ir_call_1(struct ir *ir, struct ir_value *fn, struct ir_value *arg0) {
  CHECK(ir_is_int(arg0->type));

  struct ir_instr *instr = ir_append_instr(ir, OP_CALL, VALUE_V);
  ir_set_arg0(ir, instr, fn);
  ir_set_arg1(ir, instr, arg0);
}

void ir_call_2(struct ir *ir, struct ir_value *fn, struct ir_value *arg0,
               struct ir_value *arg1) {
  CHECK(ir_is_int(arg0->type));
  CHECK(ir_is_int(arg1->type));

  struct ir_instr *instr = ir_append_instr(ir, OP_CALL, VALUE_V);
  ir_set_arg0(ir, instr, fn);
  ir_set_arg1(ir, instr, arg0);
  ir_set_arg2(ir, instr, arg1);
}

void ir_call_fallback(struct ir *ir, void *fallback, uint32_t addr,
                      uint32_t raw_instr) {
  CHECK(fallback);

  struct ir_instr *instr = ir_append_instr(ir, OP_CALL_FALLBACK, VALUE_V);
  ir_set_arg0(ir, instr, ir_alloc_ptr(ir, fallback));
  ir_set_arg1(ir, instr, ir_alloc_i32(ir, addr));
  ir_set_arg2(ir, instr, ir_alloc_i32(ir, raw_instr));
}

void ir_debug_info(struct ir *ir, const char *desc, uint32_t addr,
                   uint32_t raw_instr) {
  struct ir_instr *instr = ir_append_instr(ir, OP_DEBUG_INFO, VALUE_V);
  ir_set_arg0(ir, instr, ir_alloc_str(ir, desc));
  ir_set_arg1(ir, instr, ir_alloc_i32(ir, addr));
  ir_set_arg2(ir, instr, ir_alloc_i32(ir, raw_instr));
}

void ir_assert_lt(struct ir *ir, struct ir_value *a, struct ir_value *b) {
  struct ir_instr *instr = ir_append_instr(ir, OP_ASSERT_LT, VALUE_V);
  ir_set_arg0(ir, instr, a);
  ir_set_arg1(ir, instr, b);
}
