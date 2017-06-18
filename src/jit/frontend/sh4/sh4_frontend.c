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
    struct jit_opdef *def = sh4_get_opdef(data);

    addr += 2;
    block->guest_size += 2;
    block->num_cycles += def->cycles;
    block->num_instrs++;

    if (def->flags & SH4_FLAG_DELAYED) {
      uint32_t delay_data = guest->r16(guest->space, addr);
      struct jit_opdef *delay_def = sh4_get_opdef(delay_data);

      addr += 2;
      block->guest_size += 2;
      block->num_cycles += delay_def->cycles;
      block->num_instrs++;

      /* delay slots can't have another delay slot */
      CHECK(!(delay_def->flags & SH4_FLAG_DELAYED));
    }

    /* stop emitting once a branch has been hit. in addition, if fpscr has
       changed, stop emitting since the fpu state is invalidated. also, if
       sr has changed, stop emitting as there are interrupts that possibly
       need to be handled */
    if (def->flags & (SH4_FLAG_SET_PC | SH4_FLAG_SET_FPSCR | SH4_FLAG_SET_SR)) {
      break;
    }
  }
}

static const struct jit_opdef *sh4_frontend_lookup_op(struct jit_frontend *base,
                                                      const void *instr) {
  return sh4_get_opdef(*(const uint16_t *)instr);
}

static void sh4_frontend_dump_code(struct jit_frontend *base, uint32_t addr,
                                   int size) {
  struct sh4_frontend *frontend = (struct sh4_frontend *)base;
  struct jit_guest *guest = frontend->jit->guest;

  char buffer[128];

  uint32_t end = addr + size;

  while (addr < end) {
    uint16_t data = guest->r16(guest->space, addr);
    union sh4_instr instr = {data};
    struct jit_opdef *def = sh4_get_opdef(data);

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

static void sh4_frontend_translate_code(struct jit_frontend *base,
                                        struct jit_block *block,
                                        struct ir *ir) {
  struct sh4_frontend *frontend = (struct sh4_frontend *)base;
  struct sh4_guest *guest = (struct sh4_guest *)frontend->jit->guest;
  struct sh4_context *ctx = (struct sh4_context *)guest->ctx;

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
  int end_flags = 0;

  while (addr < end) {
    uint16_t data = guest->r16(guest->space, addr);
    struct jit_opdef *def = sh4_get_opdef(data);
    sh4_translate_cb cb = sh4_get_translator(data);
    union sh4_instr instr = {data};

    cb(guest, ir, flags, addr, instr);

    if (def->flags & SH4_FLAG_DELAYED) {
      addr += 4;
    } else {
      addr += 2;
    }

    end_flags = def->flags;
  }

  /* there are 3 possible block endings:

     a.) the block terminates due to an unconditional branch; nothing needs to
         be done

     b.) the block terminates due to an instruction which doesn't set the pc; an
         unconditional branch to the next address needs to be added

     c.) the block terminates due to an instruction which sets the pc but is not
         a branch (e.g. an invalid instruction trap); nothing needs to be done,
         the backend will always implicitly branch to the next pc */

  /* if the final instruction doesn't unconditionally set the pc, insert a
     branch to the next instruction */
  if ((end_flags & (SH4_FLAG_SET_PC | SH4_FLAG_COND)) != SH4_FLAG_SET_PC) {
    struct ir_block *tail_block =
        list_last_entry(&ir->blocks, struct ir_block, it);
    struct ir_instr *tail_instr =
        list_last_entry(&tail_block->instrs, struct ir_instr, it);
    ir_set_current_instr(ir, tail_instr);
    ir_branch(ir, ir_alloc_i32(ir, addr));
  }

  PROF_LEAVE();
}

static void sh4_frontend_destroy(struct jit_frontend *base) {
  struct sh4_frontend *frontend = (struct sh4_frontend *)base;

  free(frontend);
}

static void sh4_frontend_init(struct jit_frontend *base) {}

struct jit_frontend *sh4_frontend_create() {
  struct sh4_frontend *frontend = calloc(1, sizeof(struct sh4_frontend));

  frontend->init = &sh4_frontend_init;
  frontend->destroy = &sh4_frontend_destroy;
  frontend->translate_code = &sh4_frontend_translate_code;
  frontend->dump_code = &sh4_frontend_dump_code;
  frontend->lookup_op = &sh4_frontend_lookup_op;

  return (struct jit_frontend *)frontend;
}
