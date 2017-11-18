#include "jit/frontend/sh4/sh4_frontend.h"
#include "jit/frontend/sh4/sh4_disasm.h"
#include "jit/frontend/sh4/sh4_fallback.h"
#include "jit/frontend/sh4/sh4_guest.h"
#include "jit/frontend/sh4/sh4_translate.h"
#include "jit/ir/ir.h"
#include "jit/jit.h"
#include "jit/jit_frontend.h"
#include "jit/jit_guest.h"

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
    uint16_t data = guest->r16(guest->mem, addr);
    struct jit_opdef *def = sh4_get_opdef(data);

    offset += 2;
    all_flags |= def->flags;

    if (def->flags & SH4_FLAG_DELAYED) {
      uint32_t delay_addr = begin_addr + offset;
      uint16_t delay_data = guest->r16(guest->mem, delay_addr);
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
    uint16_t data = guest->r16(guest->mem, addr);
    union sh4_instr instr = {data};
    struct jit_opdef *def = sh4_get_opdef(data);

    sh4_format(addr, instr, buffer, sizeof(buffer));
    fprintf(output, "# %s\n", buffer);

    offset += 2;

    if (def->flags & SH4_FLAG_DELAYED) {
      uint32_t delay_addr = begin_addr + offset;
      uint16_t delay_data = guest->r16(guest->mem, delay_addr);
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

  int offset = 0;
  int use_fpscr = 0;
  int was_delay = 0;

  /* append inital block */
  struct ir_block *block = ir_append_block(ir);

  /* generate code specialized for the current fpscr state */
  int flags = 0;
  if (ctx->fpscr & PR_MASK) {
    flags |= SH4_DOUBLE_PR;
  }
  if (ctx->fpscr & SZ_MASK) {
    flags |= SH4_DOUBLE_SZ;
  }

  /* cheap idle skip. in an idle loop, the block is just spinning, waiting for
     an interrupt such as vblank before it'll exit. scale the block's number of
     cycles in order to yield execution faster, enabling the interrupt to
     actually be generated */
  int idle_loop = sh4_frontend_is_idle_loop(frontend, begin_addr);
  int cycle_scale = idle_loop ? 8 : 1;

  while (offset < size) {
    /* if a branch instruction / delay slot was just emitted, rewind and emit
       the delay slot as its own instruction */
    if (was_delay) {
      offset -= 2;
      was_delay = 0;
    }

    uint32_t addr = begin_addr + offset;
    uint16_t data = guest->r16(guest->mem, addr);
    union sh4_instr instr = {data};
    struct jit_opdef *def = sh4_get_opdef(data);

    use_fpscr |= (def->flags & SH4_FLAG_USE_FPSCR) == SH4_FLAG_USE_FPSCR;

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

    /* emit the instruction's translation if available */
    sh4_translate_cb cb = sh4_get_translator(data);

    if (cb) {
      /* if the instruction has a delay slot, delay_point is assigned where the
         slot's translation should be emitted */
      struct ir_insert_point delay_point;
      cb(guest, ir, addr, instr, flags, &delay_point);

      offset += 2;

      if (def->flags & SH4_FLAG_DELAYED) {
        uint32_t delay_addr = begin_addr + offset;
        uint32_t delay_data = guest->r16(guest->mem, delay_addr);
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

        /* emit the delay slot's translation if available */
        sh4_translate_cb delay_cb = sh4_get_translator(delay_data);

        if (delay_cb) {
          delay_cb(guest, ir, delay_addr, delay_instr, flags, NULL);
        } else {
          ir_fallback(ir, delay_def->fallback, delay_addr, delay_data);
        }

        /* restore insert point */
        ir_set_insert_point(ir, &original);

        offset += 2;
        was_delay = 1;
      }
    } else {
      ir_fallback(ir, def->fallback, addr, data);

      offset += 2;

      /* don't emit a fallback for the delay slot, the original fallback will
         execute it */
      if (def->flags & SH4_FLAG_DELAYED) {
        offset += 2;
        was_delay = 1;
      }
    }

    /* there are 3 possible block endings:

       1.) the block terminates due to an unconditional branch; nothing needs to
           be done

       2.) the block terminates due to an instruction which doesn't set the pc;
           an unconditional branch to the next address needs to be added

       3.) the block terminates due to an instruction which sets the pc but is
           not a branch (e.g. an invalid instruction trap); nothing needs to be
           done dispatch will always implicitly branch to the next pc */
    int store_pc = (def->flags & SH4_FLAG_STORE_PC) == SH4_FLAG_STORE_PC;
    int end_of_block = sh4_frontend_is_terminator(def) || offset >= size;

    if (end_of_block) {
      if (!store_pc) {
        struct ir_block *tail_block =
            list_last_entry(&ir->blocks, struct ir_block, it);
        struct ir_instr *tail_instr =
            list_last_entry(&tail_block->instrs, struct ir_instr, it);
        ir_set_current_instr(ir, tail_instr);

        uint32_t next_addr = begin_addr + offset;
        ir_branch(ir, ir_alloc_i32(ir, next_addr));
      }
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
}

static void sh4_frontend_analyze_code(struct jit_frontend *base,
                                      uint32_t begin_addr, int *size) {
  struct sh4_frontend *frontend = (struct sh4_frontend *)base;
  struct sh4_guest *guest = (struct sh4_guest *)frontend->guest;

  *size = 0;

  while (1) {
    uint32_t addr = begin_addr + *size;
    uint16_t data = guest->r16(guest->mem, addr);
    struct jit_opdef *def = sh4_get_opdef(data);

    *size += 2;

    if (def->flags & SH4_FLAG_DELAYED) {
      uint32_t delay_addr = begin_addr + *size;
      uint16_t delay_data = guest->r16(guest->mem, delay_addr);
      struct jit_opdef *delay_def = sh4_get_opdef(delay_data);

      *size += 2;

      /* delay slots can't have another delay slot */
      CHECK(!(delay_def->flags & SH4_FLAG_DELAYED));
    }

    if (sh4_frontend_is_terminator(def)) {
      break;
    }
  }
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
