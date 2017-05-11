#include "jit/passes/load_store_elimination_pass.h"
#include "jit/ir/ir.h"
#include "jit/pass_stats.h"

DEFINE_STAT(loads_removed, "context loads eliminated");
DEFINE_STAT(stores_removed, "context stores eliminated");

#define MAX_OFFSET 512

struct lse_entry {
  int offset;
  struct ir_value *value;
};

struct lse_state {
  struct lse_entry available[MAX_OFFSET];
  struct list_node it;
};

struct lse {
  struct list live_state;
  struct list free_state;

  /* points to the available state at the top of the live stack */
  struct lse_entry *available;
};

static void lse_pop_state(struct lse *lse) {
  /* pop top state from live list */
  struct lse_state *state =
      list_last_entry(&lse->live_state, struct lse_state, it);
  CHECK_NOTNULL(state);
  list_remove(&lse->live_state, &state->it);

  /* push state back to free list */
  list_add(&lse->free_state, &state->it);

  /* cache off current state's available member */
  state = list_last_entry(&lse->live_state, struct lse_state, it);
  lse->available = state ? state->available : NULL;
}

static void lse_push_state(struct lse *lse, int copy_from_prev) {
  struct lse_state *state =
      list_first_entry(&lse->free_state, struct lse_state, it);

  if (state) {
    /* remove from the free list */
    list_remove(&lse->free_state, &state->it);
  } else {
    /* allocate new state if one wasn't available on the free list */
    state = calloc(1, sizeof(struct lse_state));
  }

  /* push state to live list */
  list_add(&lse->live_state, &state->it);

  /* copy previous state into new state */
  if (copy_from_prev && lse->available) {
    memcpy(state->available, lse->available, sizeof(state->available));
  } else {
    memset(state->available, 0, sizeof(state->available));
  }

  /* cache off current state's available member */
  lse->available = state->available;
}

static void lse_clear_available(struct lse *lse) {
  CHECK_NOTNULL(lse->available);
  memset(lse->available, 0, sizeof(struct lse_entry) * MAX_OFFSET);
}

static void lse_erase_available(struct lse *lse, int offset, int size) {
  CHECK_LT(offset + size, MAX_OFFSET);

  int begin = offset;
  int end = offset + size - 1;

  /* if the invalidation range intersects with an entry, merge that entry into
     the invalidation range */
  struct lse_entry *begin_entry = &lse->available[begin];
  struct lse_entry *end_entry = &lse->available[end];

  if (begin_entry->value) {
    begin = begin_entry->offset;
  }

  if (end_entry->value) {
    end = end_entry->offset + ir_type_size(end_entry->value->type) - 1;
  }

  for (; begin <= end; begin++) {
    struct lse_entry *entry = &lse->available[begin];
    entry->offset = 0;
    entry->value = NULL;
  }
}

static struct ir_value *lse_get_available(struct lse *lse, int offset) {
  CHECK_LT(offset, MAX_OFFSET);

  struct lse_entry *entry = &lse->available[offset];

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
    entry->offset = offset;
    entry->value = v;
  }
}

static void lse_eliminate_loads_r(struct lse *lse, struct ir *ir,
                                  struct ir_block *block) {
  list_for_each_entry_safe(instr, &block->instrs, struct ir_instr, it) {
    if (instr->op == OP_FALLBACK) {
      lse_clear_available(lse);
    } else if (instr->op == OP_LABEL) {
      lse_clear_available(lse);
    } else if (instr->op == OP_BRANCH) {
      if (instr->arg[0]->type != VALUE_BLOCK) {
        lse_clear_available(lse);
      }
    } else if (instr->op == OP_BRANCH_TRUE || instr->op == OP_BRANCH_FALSE) {
      if (instr->arg[1]->type != VALUE_BLOCK) {
        lse_clear_available(lse);
      }
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

  list_for_each_entry(edge, &block->outgoing, struct ir_edge, it) {
    lse_push_state(lse, 1);

    lse_eliminate_loads_r(lse, ir, edge->dst);

    lse_pop_state(lse);
  }
}

static void lse_eliminate_loads(struct lse *lse, struct ir *ir) {
  lse_push_state(lse, 0);

  struct ir_block *head_block =
      list_first_entry(&ir->blocks, struct ir_block, it);
  lse_eliminate_loads_r(lse, ir, head_block);

  lse_pop_state(lse);

  CHECK(list_empty(&lse->live_state));
}

static void lse_eliminate_stores_r(struct lse *lse, struct ir *ir,
                                   struct ir_block *block) {
  struct lse_entry *parent_entries = lse->available;

  list_for_each_entry(edge, &block->outgoing, struct ir_edge, it) {
    lse_push_state(lse, 0);

    lse_eliminate_stores_r(lse, ir, edge->dst);

    /* union results from children */
    int first = !list_prev_entry(edge, struct ir_edge, it);

    for (int i = 0; i < MAX_OFFSET; i++) {
      struct lse_entry *existing = &parent_entries[i];
      struct lse_entry *child = &lse->available[i];

      if (first) {
        *existing = *child;
      } else if (memcmp(existing, child, sizeof(*existing))) {
        memset(existing, 0, sizeof(*existing));
      }
    }

    lse_pop_state(lse);
  }

  list_for_each_entry_safe_reverse(instr, &block->instrs, struct ir_instr, it) {
    if (instr->op == OP_FALLBACK) {
      lse_clear_available(lse);
    } else if (instr->op == OP_LABEL) {
      lse_clear_available(lse);
    } else if (instr->op == OP_BRANCH) {
      if (instr->arg[0]->type != VALUE_BLOCK) {
        lse_clear_available(lse);
      }
    } else if (instr->op == OP_BRANCH_TRUE || instr->op == OP_BRANCH_FALSE) {
      if (instr->arg[1]->type != VALUE_BLOCK) {
        lse_clear_available(lse);
      }
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

static void lse_eliminate_stores(struct lse *lse, struct ir *ir) {
  lse_push_state(lse, 0);

  struct ir_block *head_block =
      list_first_entry(&ir->blocks, struct ir_block, it);
  lse_eliminate_stores_r(lse, ir, head_block);

  lse_pop_state(lse);
}

void lse_run(struct lse *lse, struct ir *ir) {
  lse_eliminate_loads(lse, ir);
  lse_eliminate_stores(lse, ir);
}

void lse_destroy(struct lse *lse) {
  CHECK(list_empty(&lse->live_state));

  list_for_each_entry_safe(state, &lse->free_state, struct lse_state, it) {
    free(state);
  }

  free(lse);
}

struct lse *lse_create() {
  struct lse *lse = calloc(1, sizeof(struct lse));

  return lse;
}
