#include "jit/passes/load_store_elimination_pass.h"
#include "jit/ir/ir.h"
#include "jit/pass_stats.h"

DEFINE_PASS_STAT(loads_removed, "context loads eliminated");
DEFINE_PASS_STAT(stores_removed, "context stores eliminated");

struct lse_entry {
  /* cache token when this entry was added */
  uint64_t token;

  int offset;
  struct ir_value *value;
};

struct lse {
  /* current cache token */
  uint64_t token;

  struct lse_entry available[IR_MAX_CONTEXT];
};

static void lse_clear_available(struct lse *lse) {
  do {
    lse->token++;
  } while (lse->token == 0);
}

static void lse_erase_available(struct lse *lse, int offset, int size) {
  int begin = offset;
  int end = offset + size - 1;
  CHECK_LT(end, IR_MAX_CONTEXT);

  for (; begin <= end; begin++) {
    struct lse_entry *entry = &lse->available[begin];
    entry->token = 0;
    entry->offset = 0;
    entry->value = NULL;
  }
}

static void lse_set_available(struct lse *lse, int offset, struct ir_value *v) {
  int size = ir_type_size(v->type);
  int begin = offset;
  int end = offset + size - 1;
  CHECK_LT(end, IR_MAX_CONTEXT);

  for (; begin <= end; begin++) {
    struct lse_entry *entry = &lse->available[begin];
    entry->token = lse->token;
    entry->offset = offset;
    entry->value = v;
  }
}

static struct ir_value *lse_get_available(struct lse *lse, int offset) {
  CHECK_LT(offset, IR_MAX_CONTEXT);

  struct lse_entry *entry = &lse->available[offset];

  /* make sure entry isn't stale */
  if (entry->token != lse->token) {
    return NULL;
  }

  /* entries are added for the entire range of an available value to help with
     invalidation. if this entry doesn't start at the requested offset, it's
     not actually valid for reuse */
  if (entry->offset != offset) {
    return NULL;
  }

  /* validate the entry hasn't been partially invalidated */
  int size = ir_type_size(entry->value->type);

  for (int i = 0; i < size; i++) {
    struct lse_entry *entry2 = &lse->available[offset + i];
    if (entry2->value != entry->value) {
      return NULL;
    }
  }

  return entry->value;
}

static int lse_test_available(struct lse *lse, int offset, int size) {
  int begin = offset;
  int end = offset + size - 1;
  CHECK_LT(end, IR_MAX_CONTEXT);

  /* test if any combination of entries cover the entire range */
  for (; begin <= end; begin++) {
    struct lse_entry *entry = &lse->available[begin];
    if (entry->token != lse->token) {
      return 0;
    }
  }

  return 1;
}

static void lse_eliminate_loads(struct lse *lse, struct ir *ir,
                                struct ir_block *block) {
  lse_clear_available(lse);

  list_for_each_entry_safe(instr, &block->instrs, struct ir_instr, it) {
    if (instr->op == OP_FALLBACK || instr->op == OP_CALL) {
      lse_clear_available(lse);
    } else if (instr->op == OP_BRANCH || instr->op == OP_BRANCH_COND) {
      lse_clear_available(lse);
    } else if (instr->op == OP_LOAD_CONTEXT) {
      /* if there is already a value available for this offset, reuse it and
         remove this redundant load */;
      int offset = instr->arg[0]->i32;
      struct ir_value *existing = lse_get_available(lse, offset);

      if (existing && existing->type == instr->result->type) {
        ir_replace_uses(instr->result, existing);
        ir_remove_instr(ir, instr);

        STAT_loads_removed++;

        continue;
      }

      lse_set_available(lse, offset, instr->result);
    } else if (instr->op == OP_STORE_CONTEXT) {
      int offset = instr->arg[0]->i32;

      /* mark the value being stored as available */
      lse_set_available(lse, offset, instr->arg[1]);
    }
  }
}

static void lse_eliminate_stores(struct lse *lse, struct ir *ir,
                                 struct ir_block *block) {
  lse_clear_available(lse);

  list_for_each_entry_safe_reverse(instr, &block->instrs, struct ir_instr, it) {
    if (instr->op == OP_FALLBACK || instr->op == OP_CALL) {
      lse_clear_available(lse);
    } else if (instr->op == OP_BRANCH || instr->op == OP_BRANCH_COND) {
      lse_clear_available(lse);
    } else if (instr->op == OP_LOAD_CONTEXT) {
      int offset = instr->arg[0]->i32;
      int size = ir_type_size(instr->result->type);

      lse_erase_available(lse, offset, size);
    } else if (instr->op == OP_STORE_CONTEXT) {
      /* if subsequent stores overwrite this completely, kill it */
      int offset = instr->arg[0]->i32;
      int size = ir_type_size(instr->arg[1]->type);
      int overwritten = lse_test_available(lse, offset, size);

      if (overwritten) {
        ir_remove_instr(ir, instr);
        STAT_stores_removed++;
        continue;
      }

      lse_set_available(lse, offset, instr->arg[1]);
    }
  }
}

void lse_run(struct lse *lse, struct ir *ir) {
  list_for_each_entry(block, &ir->blocks, struct ir_block, it) {
    lse_eliminate_loads(lse, ir, block);
  }

  list_for_each_entry(block, &ir->blocks, struct ir_block, it) {
    lse_eliminate_stores(lse, ir, block);
  }
}

void lse_destroy(struct lse *lse) {
  free(lse);
}

struct lse *lse_create() {
  struct lse *lse = calloc(1, sizeof(struct lse));

  return lse;
}
