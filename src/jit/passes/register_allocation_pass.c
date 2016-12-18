#include "jit/passes/register_allocation_pass.h"
#include "core/mm_heap.h"
#include "jit/backend/jit_backend.h"
#include "jit/ir/ir.h"
#include "jit/pass_stats.h"

DEFINE_STAT(gprs_spilled, "gprs spilled");
DEFINE_STAT(fprs_spilled, "fprs spilled");

#define MAX_REGISTERS 32

struct interval {
  struct ir_instr *instr;
  struct ir_instr *reused;
  struct ir_use *start;
  struct ir_use *end;
  struct ir_use *next;
  int reg;
};

struct register_set {
  int free_regs[MAX_REGISTERS];
  int num_free_regs;

  struct interval *live_intervals[MAX_REGISTERS];
  int num_live_intervals;
};

struct ra {
  /* canonical backend register information */
  const struct jit_register *registers;
  int num_registers;

  /* allocation state */
  struct register_set int_registers;
  struct register_set float_registers;
  struct register_set vector_registers;

  /* intervals, keyed by register */
  struct interval intervals[MAX_REGISTERS];
};

static int ra_get_ordinal(const struct ir_instr *i) {
  return (int)i->tag;
}

static void ra_set_ordinal(struct ir_instr *i, int ordinal) {
  i->tag = (intptr_t)ordinal;
}

static int ra_pop_register(struct register_set *set) {
  if (!set->num_free_regs) {
    return NO_REGISTER;
  }
  return set->free_regs[--set->num_free_regs];
}

static void ra_push_register(struct register_set *set, int reg) {
  set->free_regs[set->num_free_regs++] = reg;
}

static bool ra_interval_cmp(const struct interval *lhs,
                            const struct interval *rhs) {
  return !lhs->next ||
         ra_get_ordinal(lhs->next->instr) < ra_get_ordinal(rhs->next->instr);
};

static struct interval *ra_head_interval(struct register_set *set) {
  if (!set->num_live_intervals) {
    return NULL;
  }

  mm_type *it = mm_find_min((mm_type *)set->live_intervals,
                            set->num_live_intervals, (mm_cmp)&ra_interval_cmp);
  return *it;
}

static struct interval *ra_tail_interval(struct register_set *set) {
  if (!set->num_live_intervals) {
    return NULL;
  }

  mm_type *it = mm_find_max((mm_type *)set->live_intervals,
                            set->num_live_intervals, (mm_cmp)&ra_interval_cmp);
  return *it;
}

static void ra_pop_head_interval(struct register_set *set) {
  mm_pop_min((mm_type *)set->live_intervals, set->num_live_intervals,
             (mm_cmp)&ra_interval_cmp);
  set->num_live_intervals--;
}

static void ra_pop_tail_interval(struct register_set *set) {
  mm_pop_max((mm_type *)set->live_intervals, set->num_live_intervals,
             (mm_cmp)&ra_interval_cmp);
  set->num_live_intervals--;
}

static void ra_insert_interval(struct register_set *set,
                               struct interval *interval) {
  set->live_intervals[set->num_live_intervals++] = interval;
  mm_push((mm_type *)set->live_intervals, set->num_live_intervals,
          (mm_cmp)&ra_interval_cmp);
}

static struct register_set *ra_get_register_set(struct ra *ra,
                                                enum ir_type type) {
  if (ir_is_int(type)) {
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

static int ra_alloc_blocked_register(struct ra *ra, struct ir *ir,
                                     struct ir_instr *instr) {
  struct ir_instr *insert_point = ir->current_instr;
  struct register_set *set = ra_get_register_set(ra, instr->result->type);

  /* spill the register who's next use is furthest away from start */
  struct interval *interval = ra_tail_interval(set);
  ra_pop_tail_interval(set);

  /* the interval's value needs to be filled back from from the stack before
     its next use */
  struct ir_use *next_use = interval->next;
  struct ir_use *prev_use = list_prev_entry(next_use, struct ir_use, it);
  CHECK(next_use,
        "Register being spilled has no next use, why wasn't it expired?");

  /* allocate a place on the stack to spill the value unless it was already
     previously spilled */
  struct ir_local *local;
  int reuse_local;

  struct ir_instr *spill_def = interval->instr->result->def;
  if (spill_def->op == OP_LOAD_LOCAL) {
    CHECK_EQ(spill_def->result->type, interval->instr->result->type);
    local = ir_reuse_local(ir, spill_def->arg[0], spill_def->result->type);
    reuse_local = 1;
  } else {
    local = ir_alloc_local(ir, interval->instr->result->type);
    reuse_local = 0;
  }

  /* insert load before next use */
  ir->current_instr = list_prev_entry(next_use->instr, struct ir_instr, it);
  struct ir_value *load_value = ir_load_local(ir, local);
  struct ir_instr *load_instr = load_value->def;

  /* assign the load a valid ordinal */
  int load_ordinal =
      ra_get_ordinal(list_prev_entry(load_instr, struct ir_instr, it)) + 1;
  CHECK_LT(load_ordinal,
           ra_get_ordinal(list_next_entry(load_instr, struct ir_instr, it)));
  ra_set_ordinal(load_instr, load_ordinal);

  /* update uses of interval->instr after the next use to use the new value
     filled from the stack. this code asssumes that the uses were previously
     sorted inside of ra_run */
  while (next_use) {
    /* cache off next next since calling set_value will modify the linked list
       pointers */
    struct ir_use *next_next_use = list_next_entry(next_use, struct ir_use, it);
    ir_replace_use(next_use, load_instr->result);
    next_use = next_next_use;
  }

  /* insert spill after prev use, note that order here is extremely important.
     interval->instr's use list has already been sorted, and when the save
     instruction is created and added as a use, the sorted order will be
     invalidated. because of this, the save instruction needs to be added after
     the load instruction has updated the sorted uses */
  if (!reuse_local) {
    struct ir_instr *after = NULL;

    if (prev_use) {
      /* there is a previous useerence, insert store after it */
      CHECK(list_next_entry(prev_use, struct ir_use, it) == NULL,
            "All future uses should have been replaced");
      after = prev_use->instr;
    } else {
      /* there is no previous use, insert store immediately after definition */
      CHECK(list_empty(&interval->instr->result->uses),
            "All future uses should have been replaced");
      after = interval->instr;
    }

    ir->current_instr = after;
    ir_store_local(ir, local, interval->instr->result);
  }

  /* since the interval that this store belongs to has now expired, there's no
     need to assign an ordinal to it */

  /* reuse the old interval */
  interval->instr = instr;
  interval->reused = NULL;
  interval->start = list_first_entry(&instr->result->uses, struct ir_use, it);
  interval->end = list_last_entry(&instr->result->uses, struct ir_use, it);
  interval->next = interval->start;
  ra_insert_interval(set, interval);

  /* reset insert point */
  ir->current_instr = insert_point;

  if (ir_is_int(instr->result->type)) {
    STAT_gprs_spilled++;
  } else {
    STAT_fprs_spilled++;
  }

  return interval->reg;
}

static int ra_alloc_free_register(struct ra *ra, struct ir_instr *instr) {
  struct register_set *set = ra_get_register_set(ra, instr->result->type);

  /* get the first free register for this value type */
  int reg = ra_pop_register(set);
  if (reg == NO_REGISTER) {
    return NO_REGISTER;
  }

  /* add interval */
  struct interval *interval = &ra->intervals[reg];
  interval->instr = instr;
  interval->reused = NULL;
  interval->start = list_first_entry(&instr->result->uses, struct ir_use, it);
  interval->end = list_last_entry(&instr->result->uses, struct ir_use, it);
  interval->next = interval->start;
  interval->reg = reg;
  ra_insert_interval(set, interval);

  return reg;
}

/* if the first argument isn't used after this instruction, its register
   can be reused to take advantage of many architectures supporting
   operations where the destination is the first argument.
   TODO could reorder arguments for communicative binary ops and do this
   with the second argument as well */
static int ra_reuse_arg_register(struct ra *ra, struct ir *ir,
                                 struct ir_instr *instr) {
  if (!instr->arg[0]) {
    return NO_REGISTER;
  }

  int prefered = instr->arg[0]->reg;
  if (prefered == NO_REGISTER) {
    return NO_REGISTER;
  }

  /* make sure the register can hold the result type */
  const struct jit_register *r = &ra->registers[prefered];
  if (!(r->value_types & (1 << instr->result->type))) {
    return NO_REGISTER;
  }

  /* if the argument's register is used after this instruction, it's not
     trivial to reuse */
  struct interval *interval = &ra->intervals[prefered];
  if (list_next_entry(interval->next, struct ir_use, it)) {
    return NO_REGISTER;
  }

  /* the argument's register is not used after the current instruction, so the
     register can be reused for the result. note, since the interval min/max
     heap does not support removal of an arbitrary interval, the interval
     removal must be deferred. since there are no more uses, the interval will
     expire on the next call to ra_expire_old_iintervals, and then immediately
     requeued by setting the reused property */
  interval->reused = instr;

  return prefered;
}

static void ra_expire_set(struct ra *ra, struct register_set *set,
                          struct ir_instr *instr) {
  while (true) {
    struct interval *interval = ra_head_interval(set);
    if (!interval) {
      break;
    }

    /* intervals are sorted by their next use, once one fails to expire or
       advance, they all will */
    if (interval->next &&
        ra_get_ordinal(interval->next->instr) >= ra_get_ordinal(instr)) {
      break;
    }

    /* remove interval from the sorted set */
    ra_pop_head_interval(set);

    /* if there are more uses, advance the next use and reinsert the interval
       into the correct position */
    if (interval->next && list_next_entry(interval->next, struct ir_use, it)) {
      interval->next = list_next_entry(interval->next, struct ir_use, it);
      ra_insert_interval(set, interval);
    }
    /* if there are no more uses, but the register has been reused by
       ra_reuse_arg_register, requeue the interval at this time */
    else if (interval->reused) {
      struct ir_instr *reused = interval->reused;
      interval->instr = reused;
      interval->reused = NULL;
      interval->start =
          list_first_entry(&reused->result->uses, struct ir_use, it);
      interval->end = list_last_entry(&reused->result->uses, struct ir_use, it);
      interval->next = interval->start;
      ra_insert_interval(set, interval);
    }
    /* if there are no other uses, free the register assigned to this
       interval */
    else {
      ra_push_register(set, interval->reg);
    }
  }
}

static void ra_expire_intervals(struct ra *ra, struct ir_instr *instr) {
  ra_expire_set(ra, &ra->int_registers, instr);
  ra_expire_set(ra, &ra->float_registers, instr);
  ra_expire_set(ra, &ra->vector_registers, instr);
}

static int use_cmp(const struct list_node *a_it, const struct list_node *b_it) {
  struct ir_use *a = list_entry(a_it, struct ir_use, it);
  struct ir_use *b = list_entry(b_it, struct ir_use, it);
  return ra_get_ordinal(a->instr) - ra_get_ordinal(b->instr);
}

static void ra_assign_ordinals(struct ir *ir) {
  /* assign each instruction an ordinal. these ordinals are used to describe
     the live range of a particular value */
  int ordinal = 0;

  list_for_each_entry(instr, &ir->instrs, struct ir_instr, it) {
    ra_set_ordinal(instr, ordinal);

    /* space out ordinals to leave available values for instructions inserted
       by ra_alloc_blocked_register. there should never be an ir op with more
       than 10 arguments to spill registers for */
    ordinal += 10;
  }
}

static void ra_init_sets(struct ra *ra, const struct jit_register *registers,
                         int num_registers) {
  ra->registers = registers;
  ra->num_registers = num_registers;

  for (int i = 0; i < ra->num_registers; i++) {
    const struct jit_register *r = &ra->registers[i];

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

void ra_run(struct ir *ir, const struct jit_register *registers,
            int num_registers) {
  struct ra ra = {0};

  ra_init_sets(&ra, registers, num_registers);

  ra_assign_ordinals(ir);

  list_for_each_entry(instr, &ir->instrs, struct ir_instr, it) {
    struct ir_value *result = instr->result;

    /* only allocate registers for results, assume constants can always be
       encoded as immediates or that the backend has registers reserved
       for storing the constants */
    if (!result) {
      continue;
    }

    /* sort the instruction's use list */
    list_sort(&result->uses, &use_cmp);

    /* expire any old intervals, freeing up the registers they claimed */
    ra_expire_intervals(&ra, instr);

    /* first, try and reuse the register of one of the incoming arguments */
    int reg = ra_reuse_arg_register(&ra, ir, instr);
    if (reg == NO_REGISTER) {
      /* else, allocate a new register for the result */
      reg = ra_alloc_free_register(&ra, instr);
      if (reg == NO_REGISTER) {
        /* if a register couldn't be allocated, spill and try again */
        reg = ra_alloc_blocked_register(&ra, ir, instr);
      }
    }

    CHECK_NE(reg, NO_REGISTER, "Failed to allocate register");
    result->reg = reg;
  }
}
