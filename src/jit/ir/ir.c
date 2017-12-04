#include <stdarg.h>
#include <stdio.h>
#include "jit/ir/ir.h"
#include "core/core.h"

const struct ir_opdef ir_opdefs[IR_NUM_OPS] = {
#define IR_OP(name, flags) {#name, flags},
#include "jit/ir/ir_ops.inc"
};

const char *ir_meta_names[IR_NUM_META] = {
    "addr", "cycles",
};

static void *ir_calloc(struct ir *ir, int size) {
  CHECK_LE(ir->used + size, ir->capacity);
  uint8_t *ptr = ir->buffer + ir->used;
  memset(ptr, 0, size);
  ir->used += size;
  return ptr;
}

static struct ir_block *ir_alloc_block(struct ir *ir) {
  struct ir_block *block = ir_calloc(ir, sizeof(struct ir_block));
  return block;
}

static struct ir_instr *ir_alloc_instr(struct ir *ir, enum ir_op op) {
  struct ir_instr *instr = ir_calloc(ir, sizeof(struct ir_instr));

  instr->op = op;

  /* initialize use links */
  for (int i = 0; i < IR_MAX_ARGS; i++) {
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

struct ir_insert_point ir_get_insert_point(struct ir *ir) {
  return ir->cursor;
}

void ir_set_insert_point(struct ir *ir, struct ir_insert_point *point) {
  if (point->instr) {
    ir_set_current_instr(ir, point->instr);
  } else {
    ir_set_current_block(ir, point->block);
  }
}

void ir_set_current_block(struct ir *ir, struct ir_block *block) {
  CHECK_NOTNULL(block);

  ir->cursor.block = block;
  ir->cursor.instr = NULL;
}

void ir_set_current_instr(struct ir *ir, struct ir_instr *instr) {
  CHECK_NOTNULL(instr);

  ir->cursor.block = NULL;
  ir->cursor.instr = instr;
}

struct ir_block *ir_insert_block(struct ir *ir, struct ir_block *after) {
  struct ir_block *block = ir_alloc_block(ir);

  list_add_after_entry(&ir->blocks, after, block, it);

  return block;
}

struct ir_block *ir_append_block(struct ir *ir) {
  struct ir_block *prev_block = NULL;

  if (ir->cursor.block) {
    prev_block = ir->cursor.block;
  } else if (ir->cursor.instr) {
    prev_block = ir->cursor.instr->block;
  }

  struct ir_block *block = ir_insert_block(ir, prev_block);
  ir_set_current_block(ir, block);
  return block;
}

struct ir_block *ir_split_block(struct ir *ir, struct ir_instr *before) {
  /* if before is the first instruction in the block, nothing to split */
  struct ir_instr *after = list_prev_entry(before, struct ir_instr, it);
  if (!after) {
    return before->block;
  }

  struct ir_block *old_block = before->block;
  struct ir_block *new_block = ir_insert_block(ir, old_block);

  /* TODO add list splice method */
  struct ir_instr *instr = before;
  after = NULL;

  while (instr) {
    struct ir_instr *next = list_next_entry(instr, struct ir_instr, it);

    /* remove from old block */
    list_remove_entry(&old_block->instrs, instr, it);

    /* add to new block */
    list_add_after_entry(&new_block->instrs, after, instr, it);
    instr->block = new_block;

    after = instr;
    instr = next;
  }

  return new_block;
}

void ir_remove_block(struct ir *ir, struct ir_block *block) {
  /* remove all instructions */
  list_for_each_entry_safe(instr, &block->instrs, struct ir_instr, it) {
    ir_remove_instr(ir, instr);
  }

  /* remove from block list */
  list_remove_entry(&ir->blocks, block, it);
}

void ir_add_edge(struct ir *ir, struct ir_block *src, struct ir_block *dst) {
  /* linked list data is intrusive, need to allocate two edge objects */
  {
    struct ir_edge *edge = ir_calloc(ir, sizeof(struct ir_edge));
    edge->src = src;
    edge->dst = dst;
    list_add(&src->outgoing, &edge->it);
  }
  {
    struct ir_edge *edge = ir_calloc(ir, sizeof(struct ir_edge));
    edge->src = src;
    edge->dst = dst;
    list_add(&dst->incoming, &edge->it);
  }
}

struct ir_instr *ir_append_instr(struct ir *ir, enum ir_op op,
                                 enum ir_type result_type) {
  /* allocate instruction and its result if needed */
  struct ir_instr *instr = ir_alloc_instr(ir, op);

  if (result_type != VALUE_V) {
    struct ir_value *result = ir_calloc(ir, sizeof(struct ir_value));
    result->type = result_type;
    result->def = instr;
    instr->result = result;
  }

  /* append after the cursor */
  struct ir_block *parent = NULL;
  struct ir_instr *after = NULL;

  if (ir->cursor.block) {
    parent = ir->cursor.block;
    after = NULL;
  } else if (ir->cursor.instr) {
    parent = ir->cursor.instr->block;
    after = ir->cursor.instr;
  } else {
    parent = ir_insert_block(ir, NULL);
  }

  instr->block = parent;
  list_add_after_entry(&parent->instrs, after, instr, it);

  /* update current position */
  ir_set_current_instr(ir, instr);

  return instr;
}

void ir_remove_instr(struct ir *ir, struct ir_instr *instr) {
  /* remove arguments from the use lists of their values */
  for (int i = 0; i < IR_MAX_ARGS; i++) {
    struct ir_value *value = instr->arg[i];

    if (value) {
      ir_remove_use(value, &instr->used[i]);
    }
  }

  /* remove from block */
  list_remove(&instr->block->instrs, &instr->it);
  instr->block = NULL;
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
      LOG_FATAL("unexpected value type");
      break;
  }
  return v;
}

struct ir_value *ir_alloc_i8(struct ir *ir, int8_t c) {
  struct ir_value *v = ir_calloc(ir, sizeof(struct ir_value));
  v->type = VALUE_I8;
  v->i8 = c;
  return v;
}

struct ir_value *ir_alloc_i16(struct ir *ir, int16_t c) {
  struct ir_value *v = ir_calloc(ir, sizeof(struct ir_value));
  v->type = VALUE_I16;
  v->i16 = c;
  return v;
}

struct ir_value *ir_alloc_i32(struct ir *ir, int32_t c) {
  struct ir_value *v = ir_calloc(ir, sizeof(struct ir_value));
  v->type = VALUE_I32;
  v->i32 = c;
  return v;
}

struct ir_value *ir_alloc_i64(struct ir *ir, int64_t c) {
  struct ir_value *v = ir_calloc(ir, sizeof(struct ir_value));
  v->type = VALUE_I64;
  v->i64 = c;
  return v;
}

struct ir_value *ir_alloc_f32(struct ir *ir, float c) {
  struct ir_value *v = ir_calloc(ir, sizeof(struct ir_value));
  v->type = VALUE_F32;
  v->f32 = c;
  return v;
}

struct ir_value *ir_alloc_f64(struct ir *ir, double c) {
  struct ir_value *v = ir_calloc(ir, sizeof(struct ir_value));
  v->type = VALUE_F64;
  v->f64 = c;
  return v;
}

struct ir_value *ir_alloc_ptr(struct ir *ir, void *c) {
  return ir_alloc_i64(ir, (uint64_t)c);
}

struct ir_value *ir_alloc_block_ref(struct ir *ir, struct ir_block *block) {
  struct ir_value *v = ir_calloc(ir, sizeof(struct ir_value));
  v->type = VALUE_BLOCK;
  v->blk = block;
  return v;
}

struct ir_local *ir_alloc_local(struct ir *ir, enum ir_type type) {
  /* align local to natural size */
  int type_size = ir_type_size(type);
  ir->locals_size = ALIGN_UP(ir->locals_size, type_size);

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

void ir_set_arg3(struct ir *ir, struct ir_instr *instr, struct ir_value *v) {
  ir_set_arg(ir, instr, 3, v);
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
      LOG_FATAL("unexpected value type");
      break;
  }
}

struct ir_value *ir_get_meta(struct ir *ir, const void *obj, int kind) {
  struct list *bkt = hash_bkt(ir->meta[kind], obj);

  hash_bkt_for_each_entry(meta, bkt, struct ir_meta, it) {
    if (meta->key == obj) {
      CHECK(ir_is_constant(meta->value));
      return meta->value;
    }
  }

  return NULL;
}

void ir_set_meta(struct ir *ir, const void *obj, int kind,
                 struct ir_value *value) {
  struct list *bkt = hash_bkt(ir->meta[kind], obj);
  struct ir_meta *meta = NULL;

  CHECK(ir_is_constant(value));

  /* reuse existing meta object if possible */
  hash_bkt_for_each_entry(it, bkt, struct ir_meta, it) {
    if (it->key == obj) {
      meta = it;
      break;
    }
  }

  if (!meta) {
    meta = ir_calloc(ir, sizeof(struct ir_meta));
    meta->key = obj;
    hash_add(bkt, &meta->it);
  }

  meta->value = value;
}

void ir_source_info(struct ir *ir, uint32_t addr, int cycles) {
  struct ir_instr *instr = ir_append_instr(ir, OP_SOURCE_INFO, VALUE_V);
  ir_set_arg0(ir, instr, ir_alloc_i32(ir, addr));
  ir_set_arg1(ir, instr, ir_alloc_i32(ir, cycles));
}

void ir_fallback(struct ir *ir, void *fallback, uint32_t addr,
                 uint32_t raw_instr) {
  CHECK(fallback);

  struct ir_instr *instr = ir_append_instr(ir, OP_FALLBACK, VALUE_V);
  ir_set_arg0(ir, instr, ir_alloc_ptr(ir, fallback));
  ir_set_arg1(ir, instr, ir_alloc_i32(ir, addr));
  ir_set_arg2(ir, instr, ir_alloc_i32(ir, raw_instr));
}

struct ir_value *ir_load_host(struct ir *ir, struct ir_value *addr,
                              enum ir_type type) {
  CHECK_EQ(VALUE_I64, addr->type);

  struct ir_instr *instr = ir_append_instr(ir, OP_LOAD_HOST, type);
  ir_set_arg0(ir, instr, addr);
  return instr->result;
}

void ir_store_host(struct ir *ir, struct ir_value *addr, struct ir_value *v) {
  CHECK_EQ(VALUE_I64, addr->type);

  struct ir_instr *instr = ir_append_instr(ir, OP_STORE_HOST, VALUE_V);
  ir_set_arg0(ir, instr, addr);
  ir_set_arg1(ir, instr, v);
}

struct ir_value *ir_load_guest(struct ir *ir, struct ir_value *addr,
                               enum ir_type type) {
  CHECK_EQ(VALUE_I32, addr->type);

  struct ir_instr *instr = ir_append_instr(ir, OP_LOAD_GUEST, type);
  ir_set_arg0(ir, instr, addr);
  return instr->result;
}

void ir_store_guest(struct ir *ir, struct ir_value *addr, struct ir_value *v) {
  CHECK_EQ(VALUE_I32, addr->type);

  struct ir_instr *instr = ir_append_instr(ir, OP_STORE_GUEST, VALUE_V);
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

struct ir_value *ir_load_context(struct ir *ir, size_t offset,
                                 enum ir_type type) {
  CHECK_LE(offset + ir_type_size(type), IR_MAX_CONTEXT);

  struct ir_instr *instr = ir_append_instr(ir, OP_LOAD_CONTEXT, type);
  ir_set_arg0(ir, instr, ir_alloc_i32(ir, (int32_t)offset));
  return instr->result;
}

void ir_store_context(struct ir *ir, size_t offset, struct ir_value *v) {
  CHECK_LE(offset + ir_type_size(v->type), IR_MAX_CONTEXT);

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

struct ir_value *ir_vmul(struct ir *ir, struct ir_value *a, struct ir_value *b,
                         enum ir_type el_type) {
  CHECK(ir_is_vector(a->type) && ir_is_vector(b->type));
  CHECK_EQ(el_type, VALUE_F32);

  struct ir_instr *instr = ir_append_instr(ir, OP_VMUL, a->type);
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

void ir_branch(struct ir *ir, struct ir_value *dst) {
  CHECK(dst->type == VALUE_I32);

  struct ir_instr *instr = ir_append_instr(ir, OP_BRANCH, VALUE_V);
  ir_set_arg0(ir, instr, dst);
}

void ir_branch_cond(struct ir *ir, struct ir_value *cond, struct ir_value *t,
                    struct ir_value *f) {
  struct ir_instr *instr = ir_append_instr(ir, OP_BRANCH_COND, VALUE_V);
  ir_set_arg0(ir, instr, t);
  ir_set_arg1(ir, instr, f);
  ir_set_arg2(ir, instr, cond);
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

void ir_call_cond(struct ir *ir, struct ir_value *cond, struct ir_value *fn) {
  struct ir_instr *instr = ir_append_instr(ir, OP_CALL_COND, VALUE_V);
  ir_set_arg0(ir, instr, fn);
  ir_set_arg1(ir, instr, cond);
}

void ir_call_cond_1(struct ir *ir, struct ir_value *cond, struct ir_value *fn,
                    struct ir_value *arg0) {
  CHECK(ir_is_int(arg0->type));

  struct ir_instr *instr = ir_append_instr(ir, OP_CALL_COND, VALUE_V);
  ir_set_arg0(ir, instr, fn);
  ir_set_arg1(ir, instr, cond);
  ir_set_arg2(ir, instr, arg0);
}

void ir_call_cond_2(struct ir *ir, struct ir_value *cond, struct ir_value *fn,
                    struct ir_value *arg0, struct ir_value *arg1) {
  CHECK(ir_is_int(arg0->type));
  CHECK(ir_is_int(arg1->type));

  struct ir_instr *instr = ir_append_instr(ir, OP_CALL_COND, VALUE_V);
  ir_set_arg0(ir, instr, fn);
  ir_set_arg1(ir, instr, cond);
  ir_set_arg2(ir, instr, arg0);
  ir_set_arg3(ir, instr, arg1);
}

void ir_debug_break(struct ir *ir) {
  ir_append_instr(ir, OP_DEBUG_BREAK, VALUE_V);
}

void ir_debug_log(struct ir *ir, struct ir_value *a, struct ir_value *b,
                  struct ir_value *c) {
  struct ir_instr *instr = ir_append_instr(ir, OP_DEBUG_LOG, VALUE_V);
  ir_set_arg0(ir, instr, a);
  ir_set_arg1(ir, instr, b);
  ir_set_arg2(ir, instr, c);
}

void ir_assert_eq(struct ir *ir, struct ir_value *a, struct ir_value *b) {
  struct ir_instr *instr = ir_append_instr(ir, OP_ASSERT_EQ, VALUE_V);
  ir_set_arg0(ir, instr, a);
  ir_set_arg1(ir, instr, b);
}

void ir_assert_lt(struct ir *ir, struct ir_value *a, struct ir_value *b) {
  struct ir_instr *instr = ir_append_instr(ir, OP_ASSERT_LT, VALUE_V);
  ir_set_arg0(ir, instr, a);
  ir_set_arg1(ir, instr, b);
}

struct ir_value *ir_copy(struct ir *ir, struct ir_value *a) {
  struct ir_instr *instr = ir_append_instr(ir, OP_COPY, a->type);
  ir_set_arg0(ir, instr, a);
  return instr->result;
}
