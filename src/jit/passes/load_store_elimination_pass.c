#include "jit/passes/load_store_elimination_pass.h"
#include "jit/ir/ir.h"
#include "jit/pass_stats.h"

DEFINE_STAT(loads_removed, "context loads eliminated");
DEFINE_STAT(stores_removed, "context stores eliminated");

#define MAX_OFFSET 512

struct lse_entry {
  /* cache token when this entry was added */
  uint64_t token;

  int offset;
  struct ir_value *value;
};

struct lse {
  /* current cache token */
  uint64_t token;

  struct lse_entry available[MAX_OFFSET];
};

static void lse_clear_available(struct lse *lse) {
  do {
    lse->token++;
  } while (lse->token == 0);
}

static void lse_erase_available(struct lse *lse, int offset, int size) {
  CHECK_LT(offset + size, MAX_OFFSET);

  int begin = offset;
  int end = offset + size - 1;

  /* if the invalidation range intersects with an entry, merge that entry into
     the invalidation range */
  struct lse_entry *begin_entry = &lse->available[begin];
  struct lse_entry *end_entry = &lse->available[end];

  if (begin_entry->token == lse->token) {
    begin = begin_entry->offset;
  }

  if (end_entry->token == lse->token) {
    end = end_entry->offset + ir_type_size(end_entry->value->type) - 1;
  }

  for (; begin <= end; begin++) {
    struct lse_entry *entry = &lse->available[begin];
    entry->token = 0;
    entry->offset = 0;
    entry->value = NULL;
  }
}

static struct ir_value *lse_get_available(struct lse *lse, int offset) {
  CHECK_LT(offset, MAX_OFFSET);

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

  return entry->value;
}

static void lse_set_available(struct lse *lse, int offset, struct ir_value *v) {
  int size = ir_type_size(v->type);
  CHECK_LT(offset + size, MAX_OFFSET);

  int begin = offset;
  int end = offset + size - 1;

  lse_erase_available(lse, offset, size);

  /* add entries for the entire range to aid in invalidation. only the initial
     entry where offset == entry.offset is valid for reuse */
  for (; begin <= end; begin++) {
    struct lse_entry *entry = &lse->available[begin];
    entry->token = lse->token;
    entry->offset = offset;
    entry->value = v;
  }
}

static void lse_eliminate_loads(struct lse *lse, struct ir *ir,
                                struct ir_block *block) {
  lse_clear_available(lse);

  list_for_each_entry_safe(instr, &block->instrs, struct ir_instr, it) {
    if (instr->op == OP_FALLBACK || instr->op == OP_CALL) {
      lse_clear_available(lse);
    } else if (instr->op == OP_BRANCH) {
      lse_clear_available(lse);
    } else if (instr->op == OP_BRANCH_TRUE || instr->op == OP_BRANCH_FALSE) {
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
    } else if (instr->op == OP_BRANCH) {
      lse_clear_available(lse);
    } else if (instr->op == OP_BRANCH_TRUE || instr->op == OP_BRANCH_FALSE) {
      lse_clear_available(lse);
    } else if (instr->op == OP_LOAD_CONTEXT) {
      int offset = instr->arg[0]->i32;
      int size = ir_type_size(instr->result->type);

      lse_erase_available(lse, offset, size);
    } else if (instr->op == OP_STORE_CONTEXT) {
      /* if subsequent stores have been made for this offset that would
         overwrite it completely, mark instruction as dead */
      int offset = instr->arg[0]->i32;
      struct ir_value *existing = lse_get_available(lse, offset);
      int existing_size = existing ? ir_type_size(existing->type) : 0;
      int store_size = ir_type_size(instr->arg[1]->type);

      if (existing_size >= store_size) {
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
