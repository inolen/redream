#include <limits.h>
#include "jit/passes/register_allocation_pass.h"
#include "core/core.h"
#include "core/list.h"
#include "jit/ir/ir.h"
#include "jit/jit_backend.h"
#include "jit/pass_stats.h"

/* second-chance binpacking register allocator based off of the paper "Quality
   and Speed in Linear-scan Register Allocation" by Omri Traub, Glenn Holloway
   and Michael D. Smith */

DEFINE_PASS_STAT(gprs_spilled, "gprs spilled");
DEFINE_PASS_STAT(fprs_spilled, "fprs spilled");

struct ra_tmp;

/* bins represent a single machine register into which temporaries are packed.
   the constraint on a bin is that it may contain only one valid temporary at
   any given time */
struct ra_bin {
  /* machine register backing this bin */
  const struct jit_register *reg;

  /* current temporary packed in this bin */
  int tmp_idx;
};

/* tmps represent a register allocation candidate

   on start, a temporary is created for each instruction result. the temporary
   is assigned the result's ir_value as its original location. however, the
   temporary may end up living in multiple locations during its lifetime

   when register pressure is high, a temporary may be spilled to the stack, at
   which point its value becomes NULL, and the slot becomes non-NULL

   before the temporary's next use, a fill back from the stack is inserted,
   producing a new non-NULL value to allocate for, but not touching the stack
   slot. slots are not reused by different temporaries, so once it has spilled
   once, it should not be spilled again */
struct ra_tmp {
  int first_use_idx;
  int last_use_idx;
  int next_use_idx;

  /* current location of temporary */
  struct ir_value *value;
  struct ir_local *slot;
};

/* uses represent a use of a temporary by an instruction */
struct ra_use {
  /* ordinal of instruction using the temporary */
  int ordinal;

  /* next use index */
  int next_idx;
};

struct ra {
  const struct jit_register *registers;
  int num_registers;
  const struct jit_emitter *emitters;
  int num_emitters;

  struct ra_bin *bins;
  struct ra_tmp *tmps;
  int num_tmps;
  int max_tmps;
  struct ra_use *uses;
  int num_uses;
  int max_uses;
};

#define NO_REGISTER -1
#define NO_TMP -1
#define NO_USE -1

#define ra_get_bin(i) &ra->bins[(i)]

#define ra_get_ordinal(i) ((int)(i)->tag)
#define ra_set_ordinal(i, ord) (i)->tag = (intptr_t)(ord)

#define ra_get_packed(b) \
  ((b)->tmp_idx == NO_TMP ? NULL : &ra->tmps[(b)->tmp_idx])
#define ra_set_packed(b, t) (b)->tmp_idx = (t) ? (int)((t)-ra->tmps) : NO_TMP

#define ra_get_tmp(v) (&ra->tmps[(v)->tag])
#define ra_set_tmp(v, t) (v)->tag = (int)((t)-ra->tmps)

static int ra_reg_can_store(const struct jit_register *reg,
                            const struct ir_value *v) {
  if (reg->flags & JIT_ALLOCATE) {
    if (ir_is_int(v->type) && v->type <= VALUE_I64) {
      return reg->flags & JIT_REG_I64;
    } else if (ir_is_float(v->type) && v->type <= VALUE_F64) {
      return reg->flags & JIT_REG_F64;
    } else if (ir_is_vector(v->type) && v->type <= VALUE_V128) {
      return reg->flags & JIT_REG_V128;
    }
  }
  return 0;
}

static void ra_add_use(struct ra *ra, struct ra_tmp *tmp, int ordinal) {
  if (ra->num_uses >= ra->max_uses) {
    /* grow array */
    int old_max = ra->max_uses;
    ra->max_uses = MAX(32, ra->max_uses * 2);
    ra->uses = realloc(ra->uses, ra->max_uses * sizeof(struct ra_use));

    /* initialize the new entries */
    memset(ra->uses + old_max, 0,
           (ra->max_uses - old_max) * sizeof(struct ra_use));
  }

  struct ra_use *use = &ra->uses[ra->num_uses];
  use->ordinal = ordinal;
  use->next_idx = NO_USE;

  /* append use to temporary's list of uses */
  if (tmp->next_use_idx == NO_USE) {
    CHECK(tmp->first_use_idx == NO_USE && tmp->last_use_idx == NO_USE);
    tmp->first_use_idx = ra->num_uses;
    tmp->last_use_idx = ra->num_uses;
    tmp->next_use_idx = ra->num_uses;
  } else {
    CHECK(tmp->first_use_idx != NO_USE && tmp->last_use_idx != NO_USE);
    struct ra_use *last_use = &ra->uses[tmp->last_use_idx];
    last_use->next_idx = ra->num_uses;
    tmp->last_use_idx = ra->num_uses;
  }

  ra->num_uses++;
}

static struct ra_tmp *ra_create_tmp(struct ra *ra, struct ir_value *value) {
  if (ra->num_tmps >= ra->max_tmps) {
    /* grow array */
    int old_max = ra->max_tmps;
    ra->max_tmps = MAX(32, ra->max_tmps * 2);
    ra->tmps = realloc(ra->tmps, ra->max_tmps * sizeof(struct ra_tmp));

    /* initialize the new entries */
    memset(ra->tmps + old_max, 0,
           (ra->max_tmps - old_max) * sizeof(struct ra_tmp));
  }

  /* reset the temporary's state, reusing the previously allocated uses array */
  struct ra_tmp *tmp = &ra->tmps[ra->num_tmps];
  tmp->first_use_idx = NO_USE;
  tmp->last_use_idx = NO_USE;
  tmp->next_use_idx = NO_USE;
  tmp->value = NULL;
  tmp->slot = NULL;

  /* assign the temporary to the value */
  value->tag = ra->num_tmps++;

  return tmp;
}

static int ra_validate_value(struct ra *ra, struct ir_value *v, int flags) {
  int valid = 0;

  /* check that the allocated register or constant is valid for the emitter */
  if (v) {
    if (ir_is_constant(v)) {
      if (v->type >= VALUE_I8 && v->type <= VALUE_I32) {
        valid |= (flags & JIT_IMM_I32) == JIT_IMM_I32;
      }
      if (v->type >= VALUE_I8 && v->type <= VALUE_I64) {
        valid |= (flags & JIT_IMM_I64) == JIT_IMM_I64;
      }
      if (v->type == VALUE_F32) {
        valid |= (flags & JIT_IMM_F32) == JIT_IMM_F32;
      }
      if (v->type >= VALUE_F32 && v->type <= VALUE_F64) {
        valid |= (flags & JIT_IMM_F64) == JIT_IMM_F64;
      }
      if (v->type == VALUE_BLOCK) {
        valid |= (flags & JIT_IMM_BLK) == JIT_IMM_BLK;
      }
    } else {
      /* check that the register flags match at least one of the types supported
         by the emitter */
      const struct jit_register *reg = &ra->registers[v->reg];
      valid |= ((flags & reg->flags) & JIT_TYPE_MASK) != 0;
    }
  } else {
    /* no argument expected */
    valid |= flags == 0;
    /* argument is optional */
    valid |= (flags & JIT_OPTIONAL) == JIT_OPTIONAL;
  }

  return valid;
}

static void ra_validate(struct ra *ra, struct ir *ir, struct ir_block *block) {
  /* validate that overlapping allocations weren't made */
  {
    size_t active_size = sizeof(struct ir_value *) * ra->num_registers;
    struct ir_value **active = alloca(active_size);
    memset(active, 0, active_size);

    list_for_each_entry_safe(instr, &block->instrs, struct ir_instr, it) {
      for (int i = 0; i < IR_MAX_ARGS; i++) {
        struct ir_value *arg = instr->arg[i];

        if (!arg || ir_is_constant(arg)) {
          continue;
        }

        /* make sure the argument is the current value in the register */
        CHECK_EQ(active[arg->reg], arg);
      }

      /* reset caller-saved registers */
      const struct ir_opdef *def = &ir_opdefs[instr->op];

      if (def->flags & IR_FLAG_CALL) {
        for (int i = 0; i < ra->num_registers; i++) {
          const struct jit_register *reg = &ra->registers[i];

          if (reg->flags & JIT_CALLER_SAVE) {
            active[i] = NULL;
          }
        }
      }

      /* mark the current result active */
      if (instr->result) {
        active[instr->result->reg] = instr->result;
      }
    }
  }

  /* validate allocation types */
  {
    list_for_each_entry_safe(instr, &block->instrs, struct ir_instr, it) {
      const struct jit_emitter *emitter = &ra->emitters[instr->op];
      const struct ir_opdef *def = &ir_opdefs[instr->op];
      int valid = 1;

      for (int i = 0; i < IR_MAX_ARGS; i++) {
        valid &= ra_validate_value(ra, instr->arg[i], emitter->arg_flags[i]);
      }

      valid &= ra_validate_value(ra, instr->result, emitter->res_flags);

      CHECK(valid, "invalid allocation for %s", def->name);
    }
  }
}

static void ra_pack_bin(struct ra *ra, struct ra_bin *bin,
                        struct ra_tmp *new_tmp) {
  struct ra_tmp *old_tmp = ra_get_packed(bin);

  if (old_tmp) {
    /* the existing temporary is no longer available in the bin's register */
    old_tmp->value = NULL;
  }

  if (new_tmp) {
    /* assign the bin's register to the new temporary */
    int reg = (int)(bin->reg - ra->registers);
    new_tmp->value->reg = reg;
  }

  ra_set_packed(bin, new_tmp);
}

static void ra_spill_tmp(struct ra *ra, struct ir *ir, struct ra_tmp *tmp,
                         struct ir_instr *before) {
  if (!tmp->slot) {
    struct ir_instr *after = list_prev_entry(before, struct ir_instr, it);
    struct ir_insert_point point = {before->block, after};
    ir_set_insert_point(ir, &point);

    tmp->slot = ir_alloc_local(ir, tmp->value->type);
    ir_store_local(ir, tmp->slot, tmp->value);

    /* track spill stats */
    if (ir_is_int(tmp->value->type)) {
      STAT_gprs_spilled++;
    } else {
      STAT_fprs_spilled++;
    }
  }

  tmp->value = NULL;
}

static void ra_spill_tmps(struct ra *ra, struct ir *ir,
                          struct ir_instr *instr) {
  const struct ir_opdef *def = &ir_opdefs[instr->op];

  /* only spill at call sites */
  if (!(def->flags & IR_FLAG_CALL)) {
    return;
  }

  /* iterate over temporaries, spilling any that would be invalidated by this
     call */
  int current_ordinal = ra_get_ordinal(instr);

  for (int i = 0; i < ra->num_tmps; i++) {
    struct ra_tmp *tmp = &ra->tmps[i];

    if (!tmp->value) {
      continue;
    }

    /* only spill caller-saved regs */
    struct ra_bin *bin = ra_get_bin(tmp->value->reg);

    if (!(bin->reg->flags & JIT_CALLER_SAVE)) {
      continue;
    }

    /* check that the temporary spans this call site */
    struct ra_use *first_use = &ra->uses[tmp->first_use_idx];
    struct ra_use *last_use = &ra->uses[tmp->last_use_idx];

    /* if this call site produced the temporary, no need to spill */
    if (first_use->ordinal >= current_ordinal) {
      continue;
    }

    /* if this call site is the last use of the temporary, no need to spill */
    if (last_use->ordinal <= current_ordinal) {
      continue;
    }

    /* spill before current instr */
    ra_spill_tmp(ra, ir, tmp, instr);

    /* free up temporary's bin */
    ra_pack_bin(ra, bin, NULL);
  }
}

static int ra_alloc_blocked_reg(struct ra *ra, struct ir *ir,
                                struct ra_tmp *tmp) {
  /* find the register who's next use is furthest away */
  struct ra_bin *spill_bin = NULL;
  int furthest_use = INT_MIN;

  for (int i = 0; i < ra->num_registers; i++) {
    struct ra_bin *bin = ra_get_bin(i);
    struct ra_tmp *packed = ra_get_packed(bin);

    if (!packed) {
      continue;
    }

    if (!ra_reg_can_store(bin->reg, tmp->value)) {
      continue;
    }

    struct ra_use *next_use = &ra->uses[packed->next_use_idx];

    if (next_use->ordinal > furthest_use) {
      furthest_use = next_use->ordinal;
      spill_bin = bin;
    }
  }

  if (!spill_bin) {
    return 0;
  }

  /* spill existing temporary right before the temporary being allocated for */
  struct ra_tmp *spill_tmp = ra_get_packed(spill_bin);
  ra_spill_tmp(ra, ir, spill_tmp, tmp->value->def);

  /* assign new temporary to spilled temporary's bin */
  ra_pack_bin(ra, spill_bin, tmp);

  return 1;
}

static int ra_alloc_free_reg(struct ra *ra, struct ir *ir, struct ra_tmp *tmp) {
  /* find the first free register which can store the tmp's value */
  struct ra_bin *alloc_bin = NULL;

  for (int i = 0; i < ra->num_registers; i++) {
    struct ra_bin *bin = ra_get_bin(i);
    struct ra_tmp *packed = ra_get_packed(bin);

    if (packed) {
      continue;
    }

    if (!ra_reg_can_store(bin->reg, tmp->value)) {
      continue;
    }

    alloc_bin = bin;
    break;
  }

  if (!alloc_bin) {
    return 0;
  }

  /* assign the new tmp to the register's bin */
  ra_pack_bin(ra, alloc_bin, tmp);

  return 1;
}

static int ra_reuse_arg_reg(struct ra *ra, struct ir *ir, struct ra_tmp *tmp) {
  struct ir_instr *instr = tmp->value->def;

  if (!instr->arg[0] || ir_is_constant(instr->arg[0])) {
    return 0;
  }

  /* if the argument's register is used after this instruction, it's not
     trivial to reuse */
  struct ra_tmp *arg = ra_get_tmp(instr->arg[0]);
  struct ra_use *next_use = &ra->uses[arg->next_use_idx];

  CHECK(arg->value && arg->value->reg != NO_REGISTER);

  if (next_use->next_idx != NO_USE) {
    return 0;
  }

  /* make sure the register can hold the tmp's value */
  struct ra_bin *reuse_bin = ra_get_bin(arg->value->reg);

  if (!ra_reg_can_store(reuse_bin->reg, tmp->value)) {
    return 0;
  }

  /* assign the new tmp to the register's bin */
  ra_pack_bin(ra, reuse_bin, tmp);

  return 1;
}

static void ra_alloc(struct ra *ra, struct ir *ir, struct ir_value *value) {
  if (!value) {
    return;
  }

  struct ir_instr *instr = value->def;

  /* set initial value */
  struct ra_tmp *tmp = ra_get_tmp(value);
  tmp->value = value;

  if (!ra_reuse_arg_reg(ra, ir, tmp)) {
    if (!ra_alloc_free_reg(ra, ir, tmp)) {
      if (!ra_alloc_blocked_reg(ra, ir, tmp)) {
        LOG_FATAL("failed to allocate register");
      }
    }
  }

  /* if the emitter requires arg0 to share the result register, but it wasn't
     possible to reuse the same register for each, insert a copy from arg0 to
     the result register */
  const struct jit_emitter *emitter = &ra->emitters[instr->op];
  int reuse_arg0 = emitter->res_flags & JIT_REUSE_ARG0;

  if (reuse_arg0 && tmp->value->reg != instr->arg[0]->reg) {
    struct ir_instr *copy_after = list_prev_entry(instr, struct ir_instr, it);
    ir_set_current_instr(ir, copy_after);

    /* allocate the copy the same register as the result being allocated for */
    struct ir_value *copy = ir_copy(ir, instr->arg[0]);
    copy->reg = tmp->value->reg;
  }
}

static void ra_rewrite_arg(struct ra *ra, struct ir *ir, struct ir_instr *instr,
                           int arg) {
  struct ir_use *use = &instr->used[arg];
  struct ir_value *value = *use->parg;

  if (!value || ir_is_constant(value)) {
    return;
  }

  struct ra_tmp *tmp = ra_get_tmp(value);

  /* if the value isn't currently in a register, fill it from the stack */
  if (!tmp->value) {
    CHECK_NOTNULL(tmp->slot);

    struct ir_instr *fill_after = list_prev_entry(instr, struct ir_instr, it);
    struct ir_insert_point point = {instr->block, fill_after};
    ir_set_insert_point(ir, &point);

    struct ir_value *fill = ir_load_local(ir, tmp->slot);
    int ordinal = ra_get_ordinal(instr);
    ra_set_ordinal(fill->def, ordinal - IR_MAX_ARGS + arg);
    fill->tag = value->tag;
    tmp->value = fill;

    ra_alloc(ra, ir, fill);
  }

  /* replace original value with the tmp's latest value */
  CHECK_NOTNULL(tmp->value);
  ir_replace_use(use, tmp->value);
}

static void ra_expire_tmps(struct ra *ra, struct ir *ir,
                           struct ir_instr *current) {
  int current_ordinal = ra_get_ordinal(current);

  /* free up any bins which contain tmps that have now expired */
  for (int i = 0; i < ra->num_registers; i++) {
    struct ra_bin *bin = ra_get_bin(i);
    struct ra_tmp *packed = ra_get_packed(bin);

    if (!packed) {
      continue;
    }

    while (1) {
      /* stop advancing once the next use is after the current position */
      struct ra_use *next_use = &ra->uses[packed->next_use_idx];

      if (next_use->ordinal >= current_ordinal) {
        break;
      }

      /* no more uses, expire temporary */
      if (next_use->next_idx == NO_USE) {
        ra_pack_bin(ra, bin, NULL);
        break;
      }

      packed->next_use_idx = next_use->next_idx;
    }
  }
}

static void ra_alloc_bins(struct ra *ra, struct ir *ir,
                          struct ir_block *block) {
  /* use safe iterator to avoid iterating over fills inserted
     when rewriting arguments */
  list_for_each_entry_safe(instr, &block->instrs, struct ir_instr, it) {
    /* expire temporaries that are no longer used, freeing up the bins they
       occupied for allocation */
    ra_expire_tmps(ra, ir, instr);

    /* rewrite arguments to use their temporary's latest value */
    for (int i = 0; i < IR_MAX_ARGS; i++) {
      ra_rewrite_arg(ra, ir, instr, i);
    }

    /* allocate a bin for the result */
    ra_alloc(ra, ir, instr->result);

    /* spill temporaries for caller-saved regs. note, this must come after args
       have been rewritten and the result has been allocated for. if this came
       before rewriting args, the temporaries wouldn't have a valid value to
       rewrite with. if this came before allocation, the functionality of
       ra_reuse_arg_reg would be lost */
    ra_spill_tmps(ra, ir, instr);
  }
}

static void ra_create_tmps(struct ra *ra, struct ir *ir,
                           struct ir_block *block) {
  list_for_each_entry(instr, &block->instrs, struct ir_instr, it) {
    int ordinal = ra_get_ordinal(instr);

    if (instr->result) {
      struct ra_tmp *tmp = ra_create_tmp(ra, instr->result);
      ra_add_use(ra, tmp, ordinal);
    }

    for (int i = 0; i < IR_MAX_ARGS; i++) {
      struct ir_value *arg = instr->arg[i];

      if (!arg || ir_is_constant(arg)) {
        continue;
      }

      struct ra_tmp *tmp = ra_get_tmp(arg);
      ra_add_use(ra, tmp, ordinal);
    }
  }
}

static void ra_assign_ordinals(struct ra *ra, struct ir *ir,
                               struct ir_block *block) {
  int ordinal = 0;

  /* assign each instruction an ordinal. these ordinals are used to describe
     the live range of a particular value */
  list_for_each_entry(instr, &block->instrs, struct ir_instr, it) {
    ra_set_ordinal(instr, ordinal);

    /* each instruction could fill up to IR_MAX_ARGS, space out ordinals
       enough to allow for this */
    ordinal += 1 + IR_MAX_ARGS;
  }
}

static void ra_legalize_args(struct ra *ra, struct ir *ir,
                             struct ir_block *block) {
  struct ir_instr *prev = NULL;

  list_for_each_entry_safe(instr, &block->instrs, struct ir_instr, it) {
    const struct jit_emitter *emitter = &ra->emitters[instr->op];

    for (int i = 0; i < IR_MAX_ARGS; i++) {
      struct ir_value *arg = instr->arg[i];
      int arg_flags = emitter->arg_flags[i];

      if (!arg) {
        continue;
      }

      /* legalize constants */
      if (ir_is_constant(arg)) {
        int can_encode = 0;
        can_encode |= (arg_flags & JIT_IMM_I32) &&
                      (arg->type >= VALUE_I8 && arg->type <= VALUE_I32);
        can_encode |= (arg_flags & JIT_IMM_I64) &&
                      (arg->type >= VALUE_I8 && arg->type <= VALUE_I64);
        can_encode |= (arg_flags & JIT_IMM_F32) && arg->type == VALUE_F32;
        can_encode |= (arg_flags & JIT_IMM_F64) &&
                      (arg->type >= VALUE_F32 && arg->type <= VALUE_F64);
        can_encode |= (arg_flags & JIT_IMM_BLK) && arg->type == VALUE_BLOCK;

        /* if the emitter can't encode this argument as an immediate, create a
           value for the constant and allocate a register for it */
        if (!can_encode) {
          struct ir_insert_point point = {block, prev};
          ir_set_insert_point(ir, &point);

          struct ir_value *copy = ir_copy(ir, arg);

          struct ir_use *use = &instr->used[i];
          ir_replace_use(use, copy);
        }
      }
    }

    prev = instr;
  }
}

static void ra_reset(struct ra *ra, struct ir *ir, struct ir_block *block) {
  /* reset allocation state */
  for (int i = 0; i < ra->num_registers; i++) {
    struct ra_bin *bin = &ra->bins[i];
    bin->tmp_idx = NO_TMP;
  }

  ra->num_tmps = 0;
  ra->num_uses = 0;

  /* reset register state */
  list_for_each_entry(instr, &block->instrs, struct ir_instr, it) {
    if (instr->result) {
      instr->result->reg = NO_REGISTER;
    }
  }
}

void ra_run(struct ra *ra, struct ir *ir) {
  list_for_each_entry(block, &ir->blocks, struct ir_block, it) {
    ra_reset(ra, ir, block);
    ra_legalize_args(ra, ir, block);
    ra_assign_ordinals(ra, ir, block);
    ra_create_tmps(ra, ir, block);
    ra_alloc_bins(ra, ir, block);
#if 1
    ra_validate(ra, ir, block);
#endif
  }
}

void ra_destroy(struct ra *ra) {
  free(ra->uses);
  free(ra->tmps);
  free(ra->bins);
  free(ra);
}

struct ra *ra_create(const struct jit_register *registers, int num_registers,
                     const struct jit_emitter *emitters, int num_emitters) {
  struct ra *ra = calloc(1, sizeof(struct ra));

  ra->registers = registers;
  ra->num_registers = num_registers;

  ra->emitters = emitters;
  ra->num_emitters = num_emitters;

  ra->bins = calloc(ra->num_registers, sizeof(struct ra_bin));

  for (int i = 0; i < ra->num_registers; i++) {
    struct ra_bin *bin = &ra->bins[i];
    bin->reg = &ra->registers[i];
  }

  return ra;
}
