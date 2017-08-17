#include "jit/frontend/sh4/sh4_frontend.h"
#include "core/profiler.h"
#include "jit/frontend/sh4/sh4_disasm.h"
#include "jit/frontend/sh4/sh4_fallback.h"
#include "jit/frontend/sh4/sh4_guest.h"
#include "jit/frontend/sh4/sh4_translate.h"
#include "jit/ir/ir.h"
#include "jit/jit.h"
#include "jit/jit_frontend.h"
#include "jit/jit_guest.h"

#if 0
#define LOG_ANALYZE LOG_INFO
#else
#define LOG_ANALYZE(...)
#endif

/*
 * fsca estimate lookup table, used by the jit and interpreter
 */
uint32_t sh4_fsca_table[0x20000] = {
#include "jit/frontend/sh4/sh4_fsca.inc"
};

struct sh4_frontend {
  struct jit_frontend;
};

static const struct jit_opdef *sh4_frontend_lookup_op(struct jit_frontend *base,
                                                      const void *instr) {
  return sh4_get_opdef(*(const uint16_t *)instr);
}

static int sh4_frontend_is_terminator(struct jit_opdef *def) {
  /* stop emitting once a branch is hit */
  if (def->flags & SH4_FLAG_STORE_PC) {
    return 1;
  }

  /* if fpscr changed, stop as the compile-time assumptions may be invalid */
  if (def->flags & SH4_FLAG_STORE_FPSCR) {
    return 1;
  }

  return 0;
}

static int sh4_frontend_is_idle_loop(struct sh4_frontend *frontend,
                                     uint32_t begin_addr) {
  struct sh4_guest *guest = (struct sh4_guest *)frontend->guest;

  /* look ahead to see if the current basic block is an idle loop */
  static int IDLE_MASK = SH4_FLAG_LOAD | SH4_FLAG_COND | SH4_FLAG_CMP;
  int idle_loop = 1;
  int all_flags = 0;
  int offset = 0;

  while (1) {
    uint32_t addr = begin_addr + offset;
    uint16_t data = guest->r16(guest->space, addr);
    struct jit_opdef *def = sh4_get_opdef(data);

    offset += 2;
    all_flags |= def->flags;

    if (def->flags & SH4_FLAG_DELAYED) {
      uint32_t delay_addr = begin_addr + offset;
      uint32_t delay_data = guest->r16(guest->space, delay_addr);
      struct jit_opdef *delay_def = sh4_get_opdef(delay_data);

      offset += 2;
      all_flags |= delay_def->flags;
    }

    if (sh4_frontend_is_terminator(def)) {
      /* if the block doesn't contain the required flags, disqualify */
      idle_loop &= (all_flags & IDLE_MASK) == IDLE_MASK;

      /* if the branch isn't a short back edge, disqualify */
      if (def->flags & SH4_FLAG_STORE_PC) {
        union sh4_instr instr = {data};

        int branch_type;
        uint32_t branch_addr;
        uint32_t next_addr;
        sh4_branch_info(addr, instr, &branch_type, &branch_addr, &next_addr);

        idle_loop &= (begin_addr - branch_addr) <= 32;
      }

      break;
    }
  }

  return idle_loop;
}

static void sh4_frontend_dump_code(struct jit_frontend *base,
                                   uint32_t begin_addr, int size,
                                   FILE *output) {
  struct sh4_frontend *frontend = (struct sh4_frontend *)base;
  struct jit_guest *guest = frontend->guest;

  int offset = 0;
  char buffer[128];

  fprintf(output, "#==--------------------------------------------------==#\n");
  fprintf(output, "# sh4\n");
  fprintf(output, "#==--------------------------------------------------==#\n");

  while (offset < size) {
    uint32_t addr = begin_addr + offset;
    uint16_t data = guest->r16(guest->space, addr);
    union sh4_instr instr = {data};
    struct jit_opdef *def = sh4_get_opdef(data);

    sh4_format(addr, instr, buffer, sizeof(buffer));
    fprintf(output, "# %s\n", buffer);

    offset += 2;

    if (def->flags & SH4_FLAG_DELAYED) {
      uint32_t delay_addr = begin_addr + offset;
      uint16_t delay_data = guest->r16(guest->space, delay_addr);
      union sh4_instr delay_instr = {delay_data};

      sh4_format(delay_addr, delay_instr, buffer, sizeof(buffer));
      fprintf(output, "# %s\n", buffer);

      offset += 2;
    }
  }
}

static void sh4_frontend_translate_code(struct jit_frontend *base,
                                        uint32_t begin_addr, int size,
                                        struct ir *ir) {
  struct sh4_frontend *frontend = (struct sh4_frontend *)base;
  struct sh4_guest *guest = (struct sh4_guest *)frontend->guest;
  struct sh4_context *ctx = (struct sh4_context *)guest->ctx;

  PROF_ENTER("cpu", "sh4_frontend_translate_code");

  int offset = 0;
  int in_block = 0;
  int in_delay = 0;
  int was_delay = 0;
  int use_fpscr = 0;
  int cycle_scale = 0;

  /* generate code specialized for the current fpscr state */
  int flags = 0;
  if (ctx->fpscr & PR_MASK) {
    flags |= SH4_DOUBLE_PR;
  }
  if (ctx->fpscr & SZ_MASK) {
    flags |= SH4_DOUBLE_SZ;
  }

  /* append inital block */
  struct ir_block *block = ir_append_block(ir);

  while (offset < size) {
    uint32_t addr = begin_addr + offset;
    uint16_t data = guest->r16(guest->space, addr);
    union sh4_instr instr = {data};
    struct jit_opdef *def = sh4_get_opdef(data);

    use_fpscr |= (def->flags & SH4_FLAG_USE_FPSCR) == SH4_FLAG_USE_FPSCR;
    was_delay = in_delay;
    in_delay = 0;

    /* cheap idle skip. in an idle loop, the block is just spinning, waiting for
       an interrupt such as vblank before it'll exit. scale the block's number
       of cycles in order to yield execution faster, enabling the interrupt to
       actually be generated */
    if (!in_block) {
      int idle_loop = sh4_frontend_is_idle_loop(frontend, addr);
      cycle_scale = idle_loop ? 10 : 1;
      in_block = 1;
    }

    /* emit meta information for the current guest instruction. this info is
       essential to the jit, and is used to map guest instructions to host
       addresses for branching and fastmem access */
    ir_source_info(ir, addr, def->cycles * cycle_scale);

    /* the pc is normally only written to the context at the end of the block,
       sync now for any instruction which needs to read the correct pc */
    if (def->flags & SH4_FLAG_LOAD_PC) {
      ir_store_context(ir, offsetof(struct sh4_context, pc),
                       ir_alloc_i32(ir, addr));
    }

    /* emit the instruction's translation. note, if the instruction has a delay
       slot, delay_point is assigned where the slot's translation should be
       emitted */
    struct ir_insert_point delay_point;
    sh4_translate_cb cb = sh4_get_translator(data);
    cb(guest, ir, addr, instr, flags, &delay_point);

    offset += 2;

    /* emit the delay slot's translation */
    if (def->flags & SH4_FLAG_DELAYED) {
      uint32_t delay_addr = begin_addr + offset;
      uint32_t delay_data = guest->r16(guest->space, delay_addr);
      union sh4_instr delay_instr = {delay_data};
      struct jit_opdef *delay_def = sh4_get_opdef(delay_data);

      use_fpscr |=
          (delay_def->flags & SH4_FLAG_USE_FPSCR) == SH4_FLAG_USE_FPSCR;

      /* move insert point back to the middle of the preceding instruction */
      struct ir_insert_point original = ir_get_insert_point(ir);
      ir_set_insert_point(ir, &delay_point);

      if (delay_def->flags & SH4_FLAG_LOAD_PC) {
        ir_store_context(ir, offsetof(struct sh4_context, pc),
                         ir_alloc_i32(ir, delay_addr));
      }

      sh4_translate_cb delay_cb = sh4_get_translator(delay_data);
      delay_cb(guest, ir, delay_addr, delay_instr, flags, NULL);

      /* restore insert point */
      ir_set_insert_point(ir, &original);

      /* no meta information is emitted for the delay slot, and the offset is
         not incremented, resulting in the delay slot being emitted a second
         time starting in a new block after the branch. the reason for this
         being, if the delay slot is directly branched to, the preceding
         instruction is never executed

         note note, the preceding instruction should be a branch, or else it
         wouldn't be valid to write out the delay slot a second time */
      CHECK(def->flags & SH4_FLAG_STORE_PC);
      in_delay = 1;
    }

    /* there are 3 possible block endings:

       1.) the block terminates due to a branch; nothing needs to be done

       2.) the block terminates due to an instruction which doesn't set the pc;
           a branch to the next address needs to be added

       3.) the block terminates due to an instruction which sets the pc but is
           not a branch (e.g. an invalid instruction trap); nothing needs to be
           done dispatch will always implicitly branch to the next pc */
    int store_pc = (def->flags & SH4_FLAG_STORE_PC) == SH4_FLAG_STORE_PC;
    int end_of_block = sh4_frontend_is_terminator(def) || offset >= size;

    if (end_of_block) {
      /* if the pc isn't set, branch to the next address */
      if (!store_pc) {
        struct ir_block *tail_block =
            list_last_entry(&ir->blocks, struct ir_block, it);
        struct ir_instr *tail_instr =
            list_last_entry(&tail_block->instrs, struct ir_instr, it);
        ir_set_current_instr(ir, tail_instr);

        uint32_t next_addr = begin_addr + offset;
        ir_branch(ir, ir_alloc_i32(ir, next_addr));
      }

      in_block = 0;
    }

    /* if the instruction being written was previously in a delay slot, end the
       block, forcing an entry point to be created at the return address of the
       original delayed branch. by doing this, the jit won't have to recompile
       code when the call returns to an unknown entry point

       0x100: bsr 0x200   <- delayed branch
       0x102: nop         <- delay slot, end block here
       0x104: add r0, r1  <- return address */
    if (was_delay) {
      uint32_t next_addr = begin_addr + offset;
      ir_branch(ir, ir_alloc_i32(ir, next_addr));

      in_block = 0;
    }
  }

  /* if the block makes optimizations based on the fpscr state, assert that the
     run-time fpscr state matches the compile-time state */
  if (use_fpscr) {
    /* insert after the first guest marker */
    struct ir_instr *after = NULL;
    list_for_each_entry(instr, &block->instrs, struct ir_instr, it) {
      if (instr->op == OP_SOURCE_INFO) {
        after = instr;
        break;
      }
    }
    ir_set_current_instr(ir, after);

    struct ir_value *actual =
        ir_load_context(ir, offsetof(struct sh4_context, fpscr), VALUE_I32);
    actual = ir_and(ir, actual, ir_alloc_i32(ir, PR_MASK | SZ_MASK));
    struct ir_value *expected =
        ir_alloc_i32(ir, ctx->fpscr & (PR_MASK | SZ_MASK));
    ir_assert_eq(ir, actual, expected);
  }

  PROF_LEAVE();
}

static void sh4_frontend_analyze_code(struct jit_frontend *base,
                                      uint32_t begin_addr, int *size) {
  struct sh4_frontend *frontend = (struct sh4_frontend *)base;
  struct sh4_guest *guest = (struct sh4_guest *)frontend->guest;
  uint32_t furthest_target = begin_addr;

  int offset = 0;
  int use_fpscr = 0;

  while (1) {
    uint32_t addr = begin_addr + offset;
    uint16_t data = guest->r16(guest->space, addr);
    union sh4_instr instr = {data};
    struct jit_opdef *def = sh4_get_opdef(data);
    struct jit_opdef *delay_def = NULL;

    use_fpscr |= (def->flags & SH4_FLAG_USE_FPSCR) == SH4_FLAG_USE_FPSCR;
    offset += 2;

    if (def->flags & SH4_FLAG_DELAYED) {
      uint32_t delay_addr = begin_addr + offset;
      uint16_t delay_data = guest->r16(guest->space, delay_addr);
      delay_def = sh4_get_opdef(delay_data);

      use_fpscr |= (delay_def->flags & SH4_FLAG_USE_FPSCR) == SH4_FLAG_USE_FPSCR;
      offset += 2;

      /* delay slots can't have another delay slot */
      if (delay_def->flags & SH4_FLAG_DELAYED) {
        offset -= 4;
        break;
      }
    }

    int end_of_block = sh4_frontend_is_terminator(def);

    if (end_of_block && use_fpscr) {
      /* for now, blocks that optimize based on the fpscr state must still be
         compiled individually */
      break;
    } else if (def->op == SH4_OP_BF || def->op == SH4_OP_BFS || def->op == SH4_OP_BT ||
        def->op == SH4_OP_BTS) {
      int branch_type;
      uint32_t branch_addr;
      uint32_t next_addr;
      sh4_branch_info(addr, instr, &branch_type, &branch_addr, &next_addr);

      furthest_target = MAX(furthest_target, branch_addr);

      LOG_ANALYZE("cond branch at 0x%08x furthest=0x%08x", addr,
                  furthest_target);
    } else if (def->op == SH4_OP_BRA) {
      int branch_type;
      uint32_t branch_addr;
      uint32_t next_addr;
      sh4_branch_info(addr, instr, &branch_type, &branch_addr, &next_addr);

      /* if the branch is back into the function, and there's no further target,
         consider this the end */
      if (branch_addr >= begin_addr && branch_addr < addr &&
          furthest_target <= addr) {
        LOG_ANALYZE("backwards uncond branch at 0x%08x furthest=0x%08x", addr,
                    furthest_target);
        break;
      }

      furthest_target = MAX(furthest_target, branch_addr);

      LOG_ANALYZE("uncond branch at 0x%08x furthest=0x%08x", addr,
                  furthest_target);
    } else if (def->op == SH4_OP_BSR || def->op == SH4_OP_BSRF ||
               def->op == SH4_OP_JSR) {
      /* ignore */
      LOG_ANALYZE("ignore BSR / BSRF / JSR at 0x%08x", addr);
    } else if (def->op == SH4_OP_RTS || def->op == SH4_OP_RTE) {
      /* nothing branches past this, this is the last return */
      if (furthest_target <= addr) {
        LOG_ANALYZE("caught function [0x%08x,0x%08x]", begin_addr, addr);
        break;
      }

      LOG_ANALYZE("ignore RTS / RTE at 0x%08x furthest=0x%08x", addr,
                  furthest_target);
    } else if (end_of_block) {
      /* some other terminator */
      break;
    }
  }

  *size = offset;
}

static void sh4_frontend_destroy(struct jit_frontend *base) {
  struct sh4_frontend *frontend = (struct sh4_frontend *)base;

  free(frontend);
}

struct jit_frontend *sh4_frontend_create(struct jit_guest *guest) {
  struct sh4_frontend *frontend = calloc(1, sizeof(struct sh4_frontend));

  frontend->guest = guest;
  frontend->destroy = &sh4_frontend_destroy;
  frontend->analyze_code = &sh4_frontend_analyze_code;
  frontend->translate_code = &sh4_frontend_translate_code;
  frontend->dump_code = &sh4_frontend_dump_code;
  frontend->lookup_op = &sh4_frontend_lookup_op;

  return (struct jit_frontend *)frontend;
}
