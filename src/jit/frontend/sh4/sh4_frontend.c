#include "jit/frontend/sh4/sh4_frontend.h"
#include "core/profiler.h"
#include "jit/frontend/jit_frontend.h"
#include "jit/frontend/sh4/sh4_context.h"
#include "jit/frontend/sh4/sh4_disasm.h"
#include "jit/frontend/sh4/sh4_guest.h"
#include "jit/frontend/sh4/sh4_translate.h"
#include "jit/ir/ir.h"
#include "jit/jit.h"

/*
 * fsca estimate lookup table, used by the jit and interpreter
 */
uint32_t sh4_fsca_table[0x20000] = {
#include "jit/frontend/sh4/sh4_fsca.inc"
};

struct sh4_frontend {
  struct jit_frontend;
};

static void sh4_analyze_block(const struct sh4_guest *guest, struct jit_block *block) {
  uint32_t addr = block->guest_addr;

  block->guest_size = 0;
  block->num_cycles = 0;
  block->num_instrs = 0;

  while (1) {
    struct sh4_instr instr = {0};
    instr.addr = addr;
    instr.opcode = guest->r16(guest->space, instr.addr);

    int valid = sh4_disasm(&instr);
    addr += 2;
    block->guest_size += 2;
    block->num_cycles += instr.cycles;
    block->num_instrs++;

    if (instr.flags & SH4_FLAG_DELAYED) {
      struct sh4_instr delay_instr = {0};
      delay_instr.addr = addr;
      delay_instr.opcode = guest->r16(guest->space, delay_instr.addr);

      valid = sh4_disasm(&delay_instr);
      addr += 2;
      block->guest_size += 2;
      block->num_cycles += delay_instr.cycles;
      block->num_instrs++;

      /* delay slots can't have another delay slot */
      CHECK(!(delay_instr.flags & SH4_FLAG_DELAYED));
    }

    /* end block on invalid instruction */
    if (!valid) {
      break;
    }

    /* stop emitting once a branch has been hit. in addition, if fpscr has
       changed, stop emitting since the fpu state is invalidated. also, if
       sr has changed, stop emitting as there are interrupts that possibly
       need to be handled */
    if (instr.flags &
        (SH4_FLAG_BRANCH | SH4_FLAG_SET_FPSCR | SH4_FLAG_SET_SR)) {
      break;
    }
  }
}

static void sh4_frontend_translate_code(struct jit_frontend *base,
                                        struct jit_block *block,
                                        struct ir *ir) {
  struct sh4_frontend *frontend = (struct sh4_frontend *)base;
  struct sh4_guest *guest = (struct sh4_guest *)frontend->jit->guest;
  struct sh4_ctx *ctx = (struct sh4_ctx *)guest->ctx;

  PROF_ENTER("cpu", "sh4_frontend_translate_code");

  int flags = 0;
  if (block->fastmem) {
    flags |= SH4_FASTMEM;
  }
  if (ctx->fpscr & PR_MASK) {
    flags |= SH4_DOUBLE_PR;
  }
  if (ctx->fpscr & SZ_MASK) {
    flags |= SH4_DOUBLE_SZ;
  }

  sh4_analyze_block(guest, block);

  /* translate the actual block */
  uint32_t addr = block->guest_addr;
  uint32_t end = block->guest_addr + block->guest_size;

  while (addr < end) {
    struct sh4_instr instr = {0};
    struct sh4_instr delay_instr = {0};

    instr.addr = addr;
    instr.opcode = guest->r16(guest->space, instr.addr);
    sh4_disasm(&instr);

    addr += 2;

    if (instr.flags & SH4_FLAG_DELAYED) {
      delay_instr.addr = addr;
      delay_instr.opcode = guest->r16(guest->space, delay_instr.addr);
      sh4_disasm(&delay_instr);

      addr += 2;
    }

    sh4_emit_instr(guest, ir, flags, &instr, &delay_instr);
  }

  /* if the block terminates in something other than an unconditional branch,
     fallthrough to the next pc */
  struct ir_block *tail_block =
      list_last_entry(&ir->blocks, struct ir_block, it);
  struct ir_instr *tail_instr =
      list_last_entry(&tail_block->instrs, struct ir_instr, it);

  if (tail_instr->op != OP_BRANCH) {
    ir_set_current_instr(ir, tail_instr);
    ir_branch(ir, ir_alloc_i32(ir, addr));
  }

  PROF_LEAVE();
}

static void sh4_frontend_dump_code(struct jit_frontend *base, uint32_t addr,
                                   int size) {
  struct sh4_frontend *frontend = (struct sh4_frontend *)base;
  struct jit_guest *guest = frontend->jit->guest;

  char buffer[128];

  int i = 0;

  while (i < size) {
    struct sh4_instr instr = {0};
    instr.addr = addr + i;
    instr.opcode = guest->r16(guest->space, instr.addr);
    sh4_disasm(&instr);

    sh4_format(&instr, buffer, sizeof(buffer));
    LOG_INFO(buffer);

    i += 2;

    if (instr.flags & SH4_FLAG_DELAYED) {
      struct sh4_instr delay = {0};
      delay.addr = addr + i;
      delay.opcode = guest->r16(guest->space, delay.addr);
      sh4_disasm(&delay);

      sh4_format(&delay, buffer, sizeof(buffer));
      LOG_INFO(buffer);

      i += 2;
    }
  }
}

static void sh4_frontend_init(struct jit_frontend *base) {}

void sh4_frontend_destroy(struct sh4_frontend *frontend) {
  free(frontend);
}

struct sh4_frontend *sh4_frontend_create(struct jit *jit) {
  struct sh4_frontend *frontend = calloc(1, sizeof(struct sh4_frontend));

  frontend->jit = jit;
  frontend->init = &sh4_frontend_init;
  frontend->translate_code = &sh4_frontend_translate_code;
  frontend->dump_code = &sh4_frontend_dump_code;

  return frontend;
}
