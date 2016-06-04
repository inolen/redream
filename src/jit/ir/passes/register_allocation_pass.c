#include "core/mm_heap.h"
#include "jit/backend/backend.h"
#include "jit/ir/passes/pass_stat.h"
#include "jit/ir/passes/register_allocation_pass.h"
#include "jit/ir/ir.h"

DEFINE_STAT(num_spills, "Number of registers spilled");

static const int MAX_REGISTERS = 32;

typedef struct {
  ir_instr_t *instr;
  ir_instr_t *reused;
  ir_use_t *start;
  ir_use_t *end;
  ir_use_t *next;
  int reg;
} interval_t;

typedef struct {
  int free_regs[MAX_REGISTERS];
  int num_free_regs;

  interval_t *live_intervals[MAX_REGISTERS];
  int num_live_intervals;
} register_set_t;

typedef struct {
  // canonical backend register information
  const register_def_t *registers;
  int num_registers;

  // allocation state
  register_set_t int_registers;
  register_set_t float_registers;
  register_set_t vector_registers;

  // intervals, keyed by register
  interval_t intervals[MAX_REGISTERS];
} ra_t;

static int ra_get_ordinal(const ir_instr_t *i) {
  return (int)i->tag;
}

static void ra_set_ordinal(ir_instr_t *i, int ordinal) {
  i->tag = (intptr_t)ordinal;
}

static int ra_pop_register(register_set_t *set) {
  if (!set->num_free_regs) {
    return NO_REGISTER;
  }
  return set->free_regs[--set->num_free_regs];
}

static void ra_push_register(register_set_t *set, int reg) {
  set->free_regs[set->num_free_regs++] = reg;
}

static bool ra_interval_cmp(const interval_t *lhs, const interval_t *rhs) {
  return !lhs->next ||
         ra_get_ordinal(lhs->next->instr) < ra_get_ordinal(rhs->next->instr);
};

static interval_t *ra_head_interval(register_set_t *set) {
  if (!set->num_live_intervals) {
    return NULL;
  }

  mm_type *it = mm_find_min((mm_type *)set->live_intervals,
                            set->num_live_intervals, (mm_cmp)&ra_interval_cmp);
  return *it;
}

static interval_t *ra_tail_interval(register_set_t *set) {
  if (!set->num_live_intervals) {
    return NULL;
  }

  mm_type *it = mm_find_max((mm_type *)set->live_intervals,
                            set->num_live_intervals, (mm_cmp)&ra_interval_cmp);
  return *it;
}

static void ra_pop_head_interval(register_set_t *set) {
  mm_pop_min((mm_type *)set->live_intervals, set->num_live_intervals,
             (mm_cmp)&ra_interval_cmp);
  set->num_live_intervals--;
}

static void ra_pop_tail_interval(register_set_t *set) {
  mm_pop_max((mm_type *)set->live_intervals, set->num_live_intervals,
             (mm_cmp)&ra_interval_cmp);
  set->num_live_intervals--;
}

static void ra_insert_interval(register_set_t *set, interval_t *interval) {
  set->live_intervals[set->num_live_intervals++] = interval;
  mm_push((mm_type *)set->live_intervals, set->num_live_intervals,
          (mm_cmp)&ra_interval_cmp);
}

static register_set_t *ra_get_register_set(ra_t *ra, ir_type_t type) {
  if (is_is_int(type)) {
    return &ra->int_registers;
  }

  if (ir_is_float(type)) {
    return &ra->float_registers;
  }

  if (ir_is_vector(type)) {
    return &ra->vector_registers;
  }

  LOG_FATAL("Unexpected value type");
}

static int ra_alloc_blocked_register(ra_t *ra, ir_t *ir, ir_instr_t *instr) {
  ir_instr_t *insert_point = ir->current_instr;
  register_set_t *set = ra_get_register_set(ra, instr->result->type);

  // spill the register who's next use is furthest away from start
  interval_t *interval = ra_tail_interval(set);
  ra_pop_tail_interval(set);

  // the interval's value needs to be filled back from from the stack before
  // its next use
  ir_use_t *next_use = interval->next;
  ir_use_t *prev_use = list_prev_entry(next_use, it);
  CHECK(next_use,
        "Register being spilled has no next use, why wasn't it expired?");

  // allocate a place on the stack to spill the value
  ir_local_t *local = ir_alloc_local(ir, interval->instr->result->type);

  // insert load before next use
  ir->current_instr = list_prev_entry(next_use->instr, it);
  ir_value_t *load_value = ir_load_local(ir, local);
  ir_instr_t *load_instr = load_value->def;

  // assign the load a valid ordinal
  int load_ordinal = ra_get_ordinal(list_prev_entry(load_instr, it)) + 1;
  CHECK_LT(load_ordinal, ra_get_ordinal(list_next_entry(load_instr, it)));
  ra_set_ordinal(load_instr, load_ordinal);

  // update uses of interval->instr after the next use to use the new value
  // filled from the stack. this code asssumes that the uses were previously
  // sorted inside of Run()
  while (next_use) {
    // cache off next next since calling set_value will modify the linked list
    // pointers
    ir_use_t *next_next_use = list_next_entry(next_use, it);
    ir_replace_use(next_use, load_instr->result);
    next_use = next_next_use;
  }

  // insert spill after prev use, note that order here is extremely important.
  // interval->instr's use list has already been sorted, and when the save
  // instruction is created and added as a use, the sorted order will be
  // invalidated. because of this, the save instruction needs to be added after
  // the load instruction has updated the sorted uses
  ir_instr_t *after = NULL;

  if (prev_use) {
    // there is a previous useerence, insert store after it
    CHECK(list_next_entry(prev_use, it) == NULL,
          "All future uses should have been replaced");
    after = prev_use->instr;
  } else {
    // there is no previous use, insert store immediately after definition
    CHECK(list_empty(&interval->instr->result->uses),
          "All future uses should have been replaced");
    after = interval->instr;
  }

  ir->current_instr = after;
  ir_store_local(ir, local, interval->instr->result);

  // since the interval that this store belongs to has now expired, there's no
  // need to assign an ordinal to it

  // reuse the old interval
  interval->instr = instr;
  interval->reused = NULL;
  interval->start = list_first_entry(&instr->result->uses, ir_use_t, it);
  interval->end = list_last_entry(&instr->result->uses, ir_use_t, it);
  interval->next = interval->start;
  ra_insert_interval(set, interval);

  // reset insert point
  ir->current_instr = insert_point;

  STAT_num_spills++;

  return interval->reg;
}

static int ra_alloc_free_register(ra_t *ra, ir_instr_t *instr) {
  register_set_t *set = ra_get_register_set(ra, instr->result->type);

  // get the first free register for this value type
  int reg = ra_pop_register(set);
  if (reg == NO_REGISTER) {
    return NO_REGISTER;
  }

  // add interval
  interval_t *interval = &ra->intervals[reg];
  interval->instr = instr;
  interval->reused = NULL;
  interval->start = list_first_entry(&instr->result->uses, ir_use_t, it);
  interval->end = list_last_entry(&instr->result->uses, ir_use_t, it);
  interval->next = interval->start;
  interval->reg = reg;
  ra_insert_interval(set, interval);

  return reg;
}

// If the first argument isn't used after this instruction, its register
// can be reused to take advantage of many architectures supporting
// operations where the destination is the first argument.
// TODO could reorder arguments for communicative binary ops and do this
// with the second argument as well
static int ra_reuse_arg_register(ra_t *ra, ir_t *ir, ir_instr_t *instr) {
  if (!instr->arg[0]) {
    return NO_REGISTER;
  }

  int prefered = instr->arg[0]->reg;
  if (prefered == NO_REGISTER) {
    return NO_REGISTER;
  }

  // make sure the register can hold the result type
  const register_def_t *r = &ra->registers[prefered];
  if (!(r->value_types & (1 << instr->result->type))) {
    return NO_REGISTER;
  }

  // if the argument's register is used after this instruction, it's not
  // trivial to reuse
  interval_t *interval = &ra->intervals[prefered];
  if (list_next_entry(interval->next, it)) {
    return NO_REGISTER;
  }

  // the argument's register is not used after the current instruction, so the
  // register can be reused for the result. note, since the interval min/max
  // heap does not support removal of an arbitrary interval, the interval
  // removal must be deferred. since there are no more uses, the interval will
  // expire on the next call to ExpireOldintervals, and then immediately
  // requeued by setting the reused property
  interval->reused = instr;

  return prefered;
}

static void ra_expire_set(ra_t *ra, register_set_t *set, ir_instr_t *instr) {
  while (true) {
    interval_t *interval = ra_head_interval(set);
    if (!interval) {
      break;
    }

    // intervals are sorted by their next use, once one fails to expire or
    // advance, they all will
    if (interval->next &&
        ra_get_ordinal(interval->next->instr) >= ra_get_ordinal(instr)) {
      break;
    }

    // remove interval from the sorted set
    ra_pop_head_interval(set);

    // if there are more uses, advance the next use and reinsert the interval
    // into the correct position
    if (interval->next && list_next_entry(interval->next, it)) {
      interval->next = list_next_entry(interval->next, it);
      ra_insert_interval(set, interval);
    }
    // if there are no more uses, but the register has been reused by
    // ReuseArgRegister, requeue the interval at this time
    else if (interval->reused) {
      ir_instr_t *reused = interval->reused;
      interval->instr = reused;
      interval->reused = NULL;
      interval->start = list_first_entry(&reused->result->uses, ir_use_t, it);
      interval->end = list_last_entry(&reused->result->uses, ir_use_t, it);
      interval->next = interval->start;
      ra_insert_interval(set, interval);
    }
    // if there are no other uses, free the register assigned to this
    // interval
    else {
      ra_push_register(set, interval->reg);
    }
  }
}

static void ra_expire_intervals(ra_t *ra, ir_instr_t *instr) {
  ra_expire_set(ra, &ra->int_registers, instr);
  ra_expire_set(ra, &ra->float_registers, instr);
  ra_expire_set(ra, &ra->vector_registers, instr);
}

static int use_cmp(const list_node_t *a_it, const list_node_t *b_it) {
  ir_use_t *a = list_entry(a_it, ir_use_t, it);
  ir_use_t *b = list_entry(b_it, ir_use_t, it);
  return ra_get_ordinal(a->instr) - ra_get_ordinal(b->instr);
}

static void ra_assign_ordinals(ir_t *ir) {
  // assign each instruction an ordinal. these ordinals are used to describe
  // the live range of a particular value
  int ordinal = 0;

  list_for_each_entry(instr, &ir->instrs, ir_instr_t, it) {
    ra_set_ordinal(instr, ordinal);

    // space out ordinals to leave available values for instructions inserted
    // by AllocBlockedRegister. there should never be an ir op with more than
    // 10 arguments to spill registers for
    ordinal += 10;
  }
}

static void ra_init_sets(ra_t *ra, const register_def_t *registers,
                         int num_registers) {
  ra->registers = registers;
  ra->num_registers = num_registers;

  for (int i = 0; i < ra->num_registers; i++) {
    const register_def_t *r = &ra->registers[i];

    if (r->value_types == VALUE_INT_MASK) {
      ra_push_register(&ra->int_registers, i);
    } else if (r->value_types == VALUE_FLOAT_MASK) {
      ra_push_register(&ra->float_registers, i);
    } else if (r->value_types == VALUE_VECTOR_MASK) {
      ra_push_register(&ra->vector_registers, i);
    } else {
      LOG_FATAL("Unsupported register value mask");
    }
  }
}

void ra_run(ir_t *ir, const register_def_t *registers, int num_registers) {
  ra_t ra = {};

  ra_init_sets(&ra, registers, num_registers);

  ra_assign_ordinals(ir);

  list_for_each_entry(instr, &ir->instrs, ir_instr_t, it) {
    ir_value_t *result = instr->result;

    // only allocate registers for results, assume constants can always be
    // encoded as immediates or that the backend has registers reserved
    // for storing the constants
    if (!result) {
      continue;
    }

    // sort the instruction's use list
    list_sort(&result->uses, &use_cmp);

    // expire any old intervals, freeing up the registers they claimed
    ra_expire_intervals(&ra, instr);

    // first, try and reuse the register of one of the incoming arguments
    int reg = ra_reuse_arg_register(&ra, ir, instr);
    if (reg == NO_REGISTER) {
      // else, allocate a new register for the result
      reg = ra_alloc_free_register(&ra, instr);
      if (reg == NO_REGISTER) {
        // if a register couldn't be allocated, spill a register and try again
        reg = ra_alloc_blocked_register(&ra, ir, instr);
      }
    }

    CHECK_NE(reg, NO_REGISTER, "Failed to allocate register");
    result->reg = reg;
  }
}
