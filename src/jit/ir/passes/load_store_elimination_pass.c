#include "jit/ir/passes/load_store_elimination_pass.h"
#include "jit/ir/passes/pass_stat.h"
#include "jit/ir/ir.h"

DEFINE_STAT(num_loads_removed, "Number of loads eliminated");
DEFINE_STAT(num_stores_removed, "Number of stores eliminated");

const char *lse_name = "lse";

static const int MAX_OFFSET = 512;

typedef struct {
  int offset;
  ir_value_t *value;
} available_t;

typedef struct { available_t available[MAX_OFFSET]; } lse_t;

static void lse_clear_available(lse_t *lse) {
  memset(lse->available, 0, sizeof(lse->available));
}

static ir_value_t *lse_get_available(lse_t *lse, int offset) {
  CHECK_LT(offset, MAX_OFFSET);

  available_t *entry = &lse->available[offset];

  // entries are added for the entire range of an available value to help with
  // invalidation. if this entry doesn't start at the requested offset, it's
  // not actually valid for reuse
  if (entry->offset != offset) {
    return NULL;
  }

  return entry->value;
}

static void lse_erase_available(lse_t *lse, int offset, int size) {
  int begin = offset;
  int end = offset + size - 1;

  // if the invalidation range intersects with an entry, merge that entry into
  // the invalidation range
  available_t *begin_entry = &lse->available[begin];
  available_t *end_entry = &lse->available[end];

  if (begin_entry->value) {
    begin = begin_entry->offset;
  }

  if (end_entry->value) {
    end = end_entry->offset + ir_type_size(end_entry->value->type) - 1;
  }

  for (; begin <= end; begin++) {
    available_t *entry = &lse->available[begin];
    entry->offset = 0;
    entry->value = NULL;
  }
}

static void lse_set_available(lse_t *lse, int offset, ir_value_t *v) {
  int size = ir_type_size(v->type);
  int begin = offset;
  int end = offset + size - 1;

  lse_erase_available(lse, offset, size);

  // add entries for the entire range to aid in invalidation. only the initial
  // entry where offset == entry.offset is valid for reuse
  for (; begin <= end; begin++) {
    available_t *entry = &lse->available[begin];
    entry->offset = offset;
    entry->value = v;
  }
}

void lse_run(ir_t *ir) {
  lse_t lse;

  // eliminate redundant loads
  {
    lse_clear_available(&lse);

    list_for_each_entry_safe(instr, &ir->instrs, ir_instr_t, it) {
      if (instr->op == OP_LOAD_CONTEXT) {
        // if there is already a value available for this offset, reuse it and
        // remove this redundant load
        int offset = instr->arg[0]->i32;
        ir_value_t *available = lse_get_available(&lse, offset);

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

    list_for_each_entry_safe_reverse(instr, &ir->instrs, ir_instr_t, it) {
      if (instr->op == OP_LOAD_CONTEXT) {
        int offset = instr->arg[0]->i32;
        int size = ir_type_size(instr->result->type);

        lse_erase_available(&lse, offset, size);
      } else if (instr->op == OP_STORE_CONTEXT) {
        // if subsequent stores have been made for this offset that would
        // overwrite it completely, mark instruction as dead
        int offset = instr->arg[0]->i32;
        ir_value_t *available = lse_get_available(&lse, offset);
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
