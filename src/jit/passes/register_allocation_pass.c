#include "jit/passes/register_allocation_pass.h"
#include "core/list.h"
#include "jit/backend/jit_backend.h"
#include "jit/ir/ir.h"
#include "jit/pass_stats.h"

DEFINE_STAT(gprs_spilled, "gprs spilled");
DEFINE_STAT(fprs_spilled, "fprs spilled");

#define MAX_REGISTERS 32

struct interval {
  /* register assigned to this interval */
  const struct jit_register *reg;

  struct ir_instr *instr;
  struct ir_use *first;
  struct ir_use *last;
  struct ir_use *next;

  struct list_node it;
};

struct ra {
  /* canonical backend register information */
  const struct jit_register *registers;
  int num_registers;

  /* all intervals, keyed by register */
  struct interval intervals[MAX_REGISTERS];

  /* list of registers available for allocation */
  struct list dead_intervals;

  /* list of registers currently in use, sorted by next use */
  struct list live_intervals;
};

static int ra_get_ordinal(const struct ir_instr *i) {
  return (int)i->tag;
}

static void ra_set_ordinal(struct ir_instr *i, int ordinal) {
  i->tag = (intptr_t)ordinal;
}

static int ra_reg_can_store(const struct jit_register *reg,
                            const struct ir_instr *instr) {
  int mask = 1 << instr->result->type;
  return (reg->value_types & mask) == mask;
}

static void ra_add_dead_interval(struct ra *ra, struct interval *interval) {
  list_add(&ra->dead_intervals, &interval->it);
}

static void ra_add_live_interval(struct ra *ra, struct interval *interval) {
  /* add interval to the live list, which is sorted by each interval's next
     use */
  struct list_node *after = NULL;
  list_for_each_entry(it, &ra->live_intervals, struct interval, it) {
    if (ra_get_ordinal(it->next->instr) >
        ra_get_ordinal(interval->next->instr)) {
      break;
    }
    after = &it->it;
  }
  list_add_after(&ra->live_intervals, after, &interval->it);
}

static const struct jit_register *ra_alloc_blocked_register(
    struct ra *ra, struct ir *ir, struct ir_instr *instr) {
  /* spill the register who's next use is furthest away */
  struct interval *interval = NULL;
  list_for_each_entry_reverse(it, &ra->live_intervals, struct interval, it) {
    if (ra_reg_can_store(it->reg, instr)) {
      interval = it;
      break;
    }
  }

  CHECK_NOTNULL(interval);

  /* the register's value needs to be filled back from from the stack before
     its next use */
  struct ir_instr *insert_point = ir->current_instr;
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

  /* register's previous value is now spilled, reuse the interval for the new
     value */
  interval->instr = instr;
  interval->first = list_first_entry(&instr->result->uses, struct ir_use, it);
  interval->last = list_last_entry(&instr->result->uses, struct ir_use, it);
  interval->next = interval->first;
  list_remove(&ra->live_intervals, &interval->it);
  ra_add_live_interval(ra, interval);

  /* reset insert point */
  ir->current_instr = insert_point;

  if (ir_is_int(instr->result->type)) {
    STAT_gprs_spilled++;
  } else {
    STAT_fprs_spilled++;
  }

  return interval->reg;
}

static const struct jit_register *ra_alloc_free_register(
    struct ra *ra, struct ir_instr *instr) {
  /* try to allocate the first free interval for this value type */
  struct interval *interval = NULL;
  list_for_each_entry(it, &ra->dead_intervals, struct interval, it) {
    if (ra_reg_can_store(it->reg, instr)) {
      interval = it;
      break;
    }
  }

  if (!interval) {
    return NULL;
  }

  /* make the interval live */
  interval->instr = instr;
  interval->first = list_first_entry(&instr->result->uses, struct ir_use, it);
  interval->last = list_last_entry(&instr->result->uses, struct ir_use, it);
  interval->next = interval->first;
  list_remove(&ra->dead_intervals, &interval->it);
  ra_add_live_interval(ra, interval);

  return interval->reg;
}

/* if the first argument isn't used after this instruction, its register
   can be reused to take advantage of many architectures supporting
   operations where the destination is the first argument.
   TODO could reorder arguments for communicative binary ops and do this
   with the second argument as well */
static const struct jit_register *ra_reuse_arg_register(
    struct ra *ra, struct ir *ir, struct ir_instr *instr) {
  if (!instr->arg[0] || ir_is_constant(instr->arg[0])) {
    return NULL;
  }

  int preferred = instr->arg[0]->reg;
  if (preferred == NO_REGISTER) {
    return NULL;
  }

  /* if the argument's register is used after this instruction, it's not
     trivial to reuse */
  struct interval *interval = &ra->intervals[preferred];
  if (list_next_entry(interval->next, struct ir_use, it)) {
    return NULL;
  }

  /* make sure the register can hold the result type */
  if (!ra_reg_can_store(interval->reg, instr)) {
    return NULL;
  }

  /* argument is no longer used, reuse its interval */
  interval->instr = instr;
  interval->first = list_first_entry(&instr->result->uses, struct ir_use, it);
  interval->last = list_last_entry(&instr->result->uses, struct ir_use, it);
  interval->next = interval->first;
  list_remove(&ra->live_intervals, &interval->it);
  ra_add_live_interval(ra, interval);

  return interval->reg;
}

static void ra_expire_intervals(struct ra *ra, struct ir_instr *instr) {
  list_for_each_entry_safe(interval, &ra->live_intervals, struct interval, it) {
    /* intervals are sorted by their next use, once one fails to expire or
       advance, they all will */
    if (interval->next &&
        ra_get_ordinal(interval->next->instr) >= ra_get_ordinal(instr)) {
      break;
    }

    /* remove interval from the sorted set */
    list_remove(&ra->live_intervals, &interval->it);

    /* if there are more uses, advance the next use and reinsert the interval
       into the correct position */
    if (interval->next && list_next_entry(interval->next, struct ir_use, it)) {
      interval->next = list_next_entry(interval->next, struct ir_use, it);
      ra_add_live_interval(ra, interval);
    }
    /* if there are no other uses, free the register */
    else {
      ra_add_dead_interval(ra, interval);
    }
  }
}

static int ra_use_cmp(const struct list_node *a_it,
                      const struct list_node *b_it) {
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

static void ra_reset(struct ra *ra, const struct jit_register *registers,
                     int num_registers) {
  ra->registers = registers;
  ra->num_registers = num_registers;

  /* add a dead interval for each available register */
  for (int i = 0; i < ra->num_registers; i++) {
    struct interval *interval = &ra->intervals[i];
    interval->reg = &ra->registers[i];
    ra_add_dead_interval(ra, interval);
  }
}

void ra_run(struct ir *ir, const struct jit_register *registers,
            int num_registers) {
  struct ra ra = {0};

  ra_reset(&ra, registers, num_registers);
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
    list_sort(&result->uses, &ra_use_cmp);

    /* expire any old intervals, freeing up the registers they claimed */
    ra_expire_intervals(&ra, instr);

    /* first, try and reuse the register of one of the incoming arguments */
    const struct jit_register *reg = ra_reuse_arg_register(&ra, ir, instr);
    if (!reg) {
      /* else, allocate a new register for the result */
      reg = ra_alloc_free_register(&ra, instr);
      if (!reg) {
        /* if a register couldn't be allocated, spill and try again */
        reg = ra_alloc_blocked_register(&ra, ir, instr);
      }
    }

    CHECK_NOTNULL(reg, "Failed to allocate register");
    result->reg = (int)(reg - ra.registers);
  }
}
