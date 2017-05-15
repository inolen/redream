#include "jit/frontend/sh4/sh4_frontend.h"
#include "core/profiler.h"
#include "jit/frontend/jit_frontend.h"
#include "jit/frontend/sh4/sh4_context.h"
#include "jit/frontend/sh4/sh4_disasm.h"
#include "jit/frontend/sh4/sh4_fallback.h"
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

static void sh4_analyze_block(const struct sh4_guest *guest,
                              struct jit_block *block) {
  uint32_t addr = block->guest_addr;

  block->guest_size = 0;
  block->num_cycles = 0;
  block->num_instrs = 0;

  while (1) {
    uint32_t data = guest->r16(guest->space, addr);
    struct sh4_opdef *def = sh4_get_opdef(data);
    int invalid = (def->flags & SH4_FLAG_INVALID) == SH4_FLAG_INVALID;

    addr += 2;
    block->guest_size += 2;
    block->num_cycles += def->cycles;
    block->num_instrs++;

    if (def->flags & SH4_FLAG_DELAYED) {
      uint32_t delay_data = guest->r16(guest->space, addr);
      struct sh4_opdef *delay_def = sh4_get_opdef(delay_data);
      invalid |= (delay_def->flags & SH4_FLAG_INVALID) == SH4_FLAG_INVALID;

      addr += 2;
      block->guest_size += 2;
      block->num_cycles += delay_def->cycles;
      block->num_instrs++;

      /* delay slots can't have another delay slot */
      CHECK(!(delay_def->flags & SH4_FLAG_DELAYED));
    }

    /* end block on invalid instruction */
    if (invalid) {
      break;
    }

    /* stop emitting once a branch has been hit. in addition, if fpscr has
       changed, stop emitting since the fpu state is invalidated. also, if
       sr has changed, stop emitting as there are interrupts that possibly
       need to be handled */
    if (def->flags & (SH4_FLAG_BRANCH | SH4_FLAG_SET_FPSCR | SH4_FLAG_SET_SR)) {
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
    uint16_t data = guest->r16(guest->space, addr);
    union sh4_instr instr = {data};
    struct sh4_opdef *def = sh4_get_opdef(data);
    sh4_translate_cb cb = sh4_get_translator(data);

    cb(guest, ir, flags, addr, instr);

    if (def->flags & SH4_FLAG_DELAYED) {
      addr += 4;
    } else {
      addr += 2;
    }

#if 0
    /* emit extra debug info for recc */
    if (guest->jit->dump_blocks) {
      const char *name = sh4_opdefs[instr->op].name;
      ir_debug_info(ir, name, instr->addr, instr->opcode);
    }
#endif
  }

  /* if the block terminates in something other than an unconditional branch,
     fallthrough to the next pc */
  struct ir_block *tail_block =
      list_last_entry(&ir->blocks, struct ir_block, it);
  struct ir_instr *tail_instr =
      list_last_entry(&tail_block->instrs, struct ir_instr, it);

  int ends_in_branch = tail_instr->op == OP_BRANCH;

  if (tail_instr->op == OP_FALLBACK) {
    struct sh4_opdef *def = sh4_get_opdef(tail_instr->arg[2]->i32);

    if (def->flags & SH4_FLAG_BRANCH) {
      ends_in_branch = 1;
    }
  }

  if (!ends_in_branch) {
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

  uint32_t end = addr + size / 2;

  while (addr < end) {
    uint16_t data = guest->r16(guest->space, addr);
    union sh4_instr instr = {data};
    struct sh4_opdef *def = sh4_get_opdef(data);

    sh4_format(addr, instr, buffer, sizeof(buffer));
    LOG_INFO(buffer);

    addr += 2;

    if (def->flags & SH4_FLAG_DELAYED) {
      uint16_t delay_data = guest->r16(guest->space, addr);
      union sh4_instr delay_instr = {delay_data};

      sh4_format(addr, delay_instr, buffer, sizeof(buffer));
      LOG_INFO(buffer);

      addr += 2;
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
