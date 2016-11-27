#include "jit/ir/passes/load_store_elimination_pass.h"
#include "jit/ir/ir.h"
#include "jit/ir/passes/pass_stat.h"

DEFINE_STAT(num_loads_removed, "Number of loads eliminated");
DEFINE_STAT(num_stores_removed, "Number of stores eliminated");

const char *lse_name = "lse";

#define MAX_OFFSET 16384

struct available {
  int offset;
  struct ir_value *value;
};

struct lse {
  struct available available[MAX_OFFSET];
};

static void lse_clear_available(struct lse *lse) {
  // TODO use auto-incremented token instead of memset'ing this each time
  memset(lse->available, 0, sizeof(lse->available));
}

static struct ir_value *lse_get_available(struct lse *lse, int offset) {
  CHECK_LT(offset, MAX_OFFSET);

  struct available *entry = &lse->available[offset];

  // entries are added for the entire range of an available value to help with
  // invalidation. if this entry doesn't start at the requested offset, it's
  // not actually valid for reuse
  if (entry->offset != offset) {
    return NULL;
  }

  return entry->value;
}

static void lse_erase_available(struct lse *lse, int offset, int size) {
  CHECK_LT(offset + size, MAX_OFFSET);

  int begin = offset;
  int end = offset + size - 1;

  // if the invalidation range intersects with an entry, merge that entry into
  // the invalidation range
  struct available *begin_entry = &lse->available[begin];
  struct available *end_entry = &lse->available[end];

  if (begin_entry->value) {
    begin = begin_entry->offset;
  }

  if (end_entry->value) {
    end = end_entry->offset + ir_type_size(end_entry->value->type) - 1;
  }

  for (; begin <= end; begin++) {
    struct available *entry = &lse->available[begin];
    entry->offset = 0;
    entry->value = NULL;
  }
}

static void lse_set_available(struct lse *lse, int offset, struct ir_value *v) {
  int size = ir_type_size(v->type);
  CHECK_LT(offset + size, MAX_OFFSET);

  int begin = offset;
  int end = offset + size - 1;

  lse_erase_available(lse, offset, size);

  // add entries for the entire range to aid in invalidation. only the initial
  // entry where offset == entry.offset is valid for reuse
  for (; begin <= end; begin++) {
    struct available *entry = &lse->available[begin];
    entry->offset = offset;
    entry->value = v;
  }
}

void lse_run(struct ir *ir) {
  struct lse lse;

  // eliminate redundant loads
  {
    lse_clear_available(&lse);

    list_for_each_entry_safe(instr, &ir->instrs, struct ir_instr, it) {
      if (instr->op == OP_LABEL) {
        lse_clear_available(&lse);
      } else if (instr->op == OP_LOAD_CONTEXT) {
        // if there is already a value available for this offset, reuse it and
        // remove this redundant load
        int offset = instr->arg[0]->i32;
        struct ir_value *available = lse_get_available(&lse, offset);

        if (available && available->type == instr->result->type) {
          ir_replace_uses(instr->result, available);
          ir_remove_instr(ir, instr);

          STAT_num_loads_removed++;

          continue;
        }

        lse_set_available(&lse, offset, instr->result);
      } else if (instr->op == OP_STORE_CONTEXT) {
        int offset = instr->arg[0]->i32;

        // mark the value being stored as available
        lse_set_available(&lse, offset, instr->arg[1]);
      }
    }
  }

  // eliminate dead stores
  {
    // iterate in reverse so the current instruction is the one being removed
    lse_clear_available(&lse);

    list_for_each_entry_safe_reverse(instr, &ir->instrs, struct ir_instr, it) {
      if (instr->op == OP_LABEL) {
        lse_clear_available(&lse);
      } else if (instr->op == OP_LOAD_CONTEXT) {
        int offset = instr->arg[0]->i32;
        int size = ir_type_size(instr->result->type);

        lse_erase_available(&lse, offset, size);
      } else if (instr->op == OP_STORE_CONTEXT) {
        // if subsequent stores have been made for this offset that would
        // overwrite it completely, mark instruction as dead
        int offset = instr->arg[0]->i32;
        struct ir_value *available = lse_get_available(&lse, offset);
        int available_size = available ? ir_type_size(available->type) : 0;
        int store_size = ir_type_size(instr->arg[1]->type);

        if (available_size >= store_size) {
          ir_remove_instr(ir, instr);

          STAT_num_stores_removed++;

          continue;
        }

        lse_set_available(&lse, offset, instr->arg[1]);
      }
    }
  }
}
