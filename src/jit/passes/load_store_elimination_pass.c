#include "jit/passes/load_store_elimination_pass.h"
#include "core/bitmap.h"
#include "jit/ir/ir.h"
#include "jit/pass_stats.h"

DEFINE_STAT(loads_removed, "context loads eliminated");
DEFINE_STAT(stores_removed_local, "context stores removed locally");
DEFINE_STAT(stores_killed_global, "context stores killed globally");

struct lse_entry {
  int offset;
  struct ir_value *value;
};

struct lse_node {
  struct ir_block *block;

  struct lse_entry available[IR_MAX_CONTEXT];
  DECLARE_BITMAP(stores, IR_MAX_CONTEXT);
};

struct lse {
  struct lse_node *nodes;
  int num_nodes;
  int max_nodes;

  int *worklist;
  int num_worklist;
  int max_worklist;
};

static inline void lse_set_node(struct lse *lse, struct ir_block *block,
                                struct lse_node *n) {
  int index = (int)(n - lse->nodes);
  block->tag = (intptr_t)index;
}

static inline struct lse_node *lse_get_node(struct lse *lse,
                                            struct ir_block *block) {
  int index = block->tag;
  return &lse->nodes[index];
}

static struct lse_node *lse_alloc_node(struct lse *lse,
                                       struct ir_block *block) {
  if (lse->num_nodes >= lse->max_nodes) {
    /* grow array */
    int old_max = lse->max_nodes;
    lse->max_nodes = MAX(32, lse->max_nodes * 2);
    lse->nodes = realloc(lse->nodes, lse->max_nodes * sizeof(struct lse_node));

    /* initalize new entries */
    memset(lse->nodes + old_max, 0,
           (lse->max_nodes - old_max) * sizeof(struct lse_node));
  }

  struct lse_node *n = &lse->nodes[lse->num_nodes++];
  n->block = block;
  lse_set_node(lse, block, n);

  return n;
}

static struct lse_node *lse_dequeue_node(struct lse *lse) {
  if (!lse->num_worklist) {
    return NULL;
  }
  int index = lse->worklist[--lse->num_worklist];
  return &lse->nodes[index];
}

static void lse_enqueue_node(struct lse *lse, struct lse_node *n) {
  if (lse->num_worklist >= lse->max_worklist) {
    /* grow array */
    int old_max = lse->max_worklist;
    lse->max_worklist = MAX(32, lse->max_worklist * 2);
    lse->worklist = realloc(lse->worklist, lse->max_worklist * sizeof(int));
  }

  int index = (int)(n - lse->nodes);
  lse->worklist[lse->num_worklist++] = index;
}

static void lse_clear_available(struct lse_node *n) {
  memset(n->available, 0, sizeof(n->available));
}

static void lse_erase_available(struct lse_node *n, int offset, int size) {
  int begin = offset;
  int end = offset + size - 1;
  CHECK_LT(end, IR_MAX_CONTEXT);

  for (; begin <= end; begin++) {
    struct lse_entry *entry = &n->available[begin];
    entry->offset = 0;
    entry->value = NULL;
  }
}

static void lse_set_available(struct lse_node *n, int offset,
                              struct ir_value *v) {
  int size = ir_type_size(v->type);
  int begin = offset;
  int end = offset + size - 1;
  CHECK_LT(end, IR_MAX_CONTEXT);

  for (; begin <= end; begin++) {
    struct lse_entry *entry = &n->available[begin];
    entry->offset = offset;
    entry->value = v;
  }
}

static struct ir_value *lse_get_available(struct lse_node *n, int offset) {
  CHECK_LT(offset, IR_MAX_CONTEXT);

  struct lse_entry *entry = &n->available[offset];

  if (!entry->value) {
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
    struct lse_entry *entry2 = &n->available[offset + i];
    if (entry2->value != entry->value) {
      return NULL;
    }
  }

  return entry->value;
}

static int lse_test_available(struct lse_node *n, int offset, int size) {
  int begin = offset;
  int end = offset + size - 1;
  CHECK_LT(end, IR_MAX_CONTEXT);

  /* test if any combination of entries cover the entire range */
  for (; begin <= end; begin++) {
    struct lse_entry *entry = &n->available[begin];
    if (!entry->value) {
      return 0;
    }
  }

  return 1;
}

static void lse_reset(struct lse *lse) {
  lse->num_nodes = 0;
  lse->num_worklist = 0;
}

static void lse_eliminate_block_loads(struct lse *lse, struct ir *ir,
                                      struct lse_node *n) {
  struct ir_block *block = n->block;

#if 0
  n->changed = 0;

  /* merge incoming state */
  list_for_each_entry(edge, &block->incoming, struct ir_edge, it) {
    struct lse_node *pred = lse_get_node(lse, edge->src);
  }
#endif

  list_for_each_entry_safe(instr, &block->instrs, struct ir_instr, it) {
    if (instr->op == OP_FALLBACK || instr->op == OP_CALL) {
      lse_clear_available(n);
    } else if (instr->op == OP_LOAD_CONTEXT) {
      /* if there is already a value available for this offset, reuse it and
         remove this redundant load */;
      int offset = instr->arg[0]->i32;
      struct ir_value *existing = lse_get_available(n, offset);

      if (existing && existing->type == instr->result->type) {
        ir_replace_uses(instr->result, existing);
        ir_remove_instr(ir, instr);

        STAT_loads_removed++;

        continue;
      }

      lse_set_available(n, offset, instr->result);
    } else if (instr->op == OP_STORE_CONTEXT) {
      int offset = instr->arg[0]->i32;

      /* mark the value being stored as available */
      lse_set_available(n, offset, instr->arg[1]);
    }
  }

#if 0
  if (n->changed) {
    list_for_each_entry(edge, &block->outgoing, struct ir_edge, it) {
      struct lse_node *succ = lse_get_node(lse, edge->dst);
      lse_enqueue_node(lse, succ);
    }
  }
#endif
}

static void lse_eliminate_loads(struct lse *lse, struct ir *ir) {
  lse_reset(lse);

  /* assign a node to each block and add to working list */
  list_for_each_entry(block, &ir->blocks, struct ir_block, it) {
    struct lse_node *n = lse_alloc_node(lse, block);

    /* reset initial state */
    lse_clear_available(n);

    lse_enqueue_node(lse, n);
  }

  /* process working list until work converges */
  while (1) {
    struct lse_node *n = lse_dequeue_node(lse);

    if (!n) {
      break;
    }

    lse_eliminate_block_loads(lse, ir, n);
  }
}

static void lse_eliminate_stores_local(struct lse *lse, struct ir *ir,
                                       struct ir_block *block) {
  DECLARE_BITMAP(stores, IR_MAX_CONTEXT);

  bitmap_clear(stores, 0, IR_MAX_CONTEXT);

  list_for_each_entry_safe_reverse(instr, &block->instrs, struct ir_instr, it) {
    if (instr->op == OP_FALLBACK || instr->op == OP_CALL) {
      bitmap_clear(stores, 0, IR_MAX_CONTEXT);
    } else if (instr->op == OP_LOAD_CONTEXT) {
      int offset = instr->arg[0]->i32;
      int size = ir_type_size(instr->result->type);

      bitmap_clear(stores, offset, size);
    } else if (instr->op == OP_STORE_CONTEXT) {
      int offset = instr->arg[0]->i32;
      int size = ir_type_size(instr->arg[1]->type);

      /* if subsequent stores from this block overwrite this completely,
         explicitly remove the instruction */
      int overwritten = bitmap_test(stores, offset, size);
      if (overwritten) {
        ir_remove_instr(ir, instr);
        STAT_stores_removed_local++;
        continue;
      }

      bitmap_set(stores, offset, size);
    }
  }
}

static void lse_eliminate_stores_global(struct lse *lse, struct ir *ir,
                                        struct lse_node *n) {
  struct ir_block *block = n->block;

  /* merge incoming state of each successor */
  DECLARE_BITMAP(old, IR_MAX_CONTEXT);
  bitmap_copy(old, n->stores, IR_MAX_CONTEXT);

  int first = 1;
  list_for_each_entry(edge, &block->outgoing, struct ir_edge, it) {
    struct lse_node *succ = lse_get_node(lse, edge->dst);

    if (first) {
      bitmap_copy(n->stores, succ->stores, IR_MAX_CONTEXT);
      first = 0;
    } else {
      bitmap_and(n->stores, n->stores, succ->stores, IR_MAX_CONTEXT);
    }
  }

  /* eliminate stores */
  list_for_each_entry_safe_reverse(instr, &block->instrs, struct ir_instr, it) {
    if (instr->op == OP_FALLBACK || instr->op == OP_CALL) {
      bitmap_clear(n->stores, 0, IR_MAX_CONTEXT);
    } else if (instr->op == OP_LOAD_CONTEXT) {
      int offset = instr->arg[0]->i32;
      int size = ir_type_size(instr->result->type);

      bitmap_clear(n->stores, offset, size);
    } else if (instr->op == OP_STORE_CONTEXT) {
      int offset = instr->arg[0]->i32;
      int size = ir_type_size(instr->arg[1]->type);

      /* if subsequent stores from other blocks overwrite this completely, mark
         the instruction as killed. note, this is different from the local pass
         which explicitly removes the instruction

         this happens because there is no guarantee the block overwriting this
         store will run. execution may yield early when the time slice is up or
         an interrupt is raised. stores marked with the kill tag will be skipped
         normally, but performed in the event the execution yields */
      int overwritten = bitmap_test(n->stores, offset, size);
      if (overwritten) {
        struct ir_value *meta_kill = ir_get_meta(ir, instr, IR_META_KILL);
        if (!meta_kill || !meta_kill->i32) {
          ir_set_meta(ir, instr, IR_META_KILL, ir_alloc_i32(ir, 1));
          STAT_stores_killed_global++;
        }
        continue;
      }

      bitmap_set(n->stores, offset, size);
    }
  }

  /* if available state has changed, reprocess all predecessors */
  int changed = !bitmap_equal(n->stores, old, IR_MAX_CONTEXT);

  if (changed) {
    list_for_each_entry(edge, &block->incoming, struct ir_edge, it) {
      struct lse_node *pred = lse_get_node(lse, edge->src);
      lse_enqueue_node(lse, pred);
    }
  }
}

static void lse_eliminate_stores(struct lse *lse, struct ir *ir) {
  lse_reset(lse);

  /* perform local store elimination */
  list_for_each_entry(block, &ir->blocks, struct ir_block, it) {
    lse_eliminate_stores_local(lse, ir, block);
  }

  /* perform global store elimination */
  list_for_each_entry(block, &ir->blocks, struct ir_block, it) {
    struct lse_node *n = lse_alloc_node(lse, block);

    /* initialize state */
    bitmap_clear(n->stores, 0, IR_MAX_CONTEXT);

    lse_enqueue_node(lse, n);
  }

  /* process working list until work converges */
  while (1) {
    struct lse_node *n = lse_dequeue_node(lse);

    if (!n) {
      break;
    }

    lse_eliminate_stores_global(lse, ir, n);
  }
}

void lse_run(struct lse *lse, struct ir *ir) {
  lse_eliminate_loads(lse, ir);
  lse_eliminate_stores(lse, ir);
}

void lse_destroy(struct lse *lse) {
  free(lse);
}

struct lse *lse_create() {
  struct lse *lse = calloc(1, sizeof(struct lse));

  return lse;
}
