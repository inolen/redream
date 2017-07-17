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

static const struct jit_opdef *sh4_frontend_lookup_op(struct jit_frontend *base,
                                                      const void *instr) {
  return sh4_get_opdef(*(const uint16_t *)instr);
}

static void sh4_frontend_dump_code(struct jit_frontend *base,
                                   const struct jit_block *block) {
  struct sh4_frontend *frontend = (struct sh4_frontend *)base;
  struct jit_guest *guest = frontend->jit->guest;

  char buffer[128];

  for (int offset = 0; offset < block->guest_size; offset += 2) {
    uint32_t addr = block->guest_addr + offset;
    uint16_t data = guest->r16(guest->space, addr);
    union sh4_instr instr = {data};
    struct jit_opdef *def = sh4_get_opdef(data);

    sh4_format(addr, instr, buffer, sizeof(buffer));
    LOG_INFO(buffer);

    if (def->flags & SH4_FLAG_DELAYED) {
      uint32_t delay_addr = addr + 2;
      uint16_t delay_data = guest->r16(guest->space, delay_addr);
      union sh4_instr delay_instr = {delay_data};

      sh4_format(addr, delay_instr, buffer, sizeof(buffer));
      LOG_INFO(buffer);

      offset += 2;
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
  if (ctx->fpscr & PR_MASK) {
    flags |= SH4_DOUBLE_PR;
  }
  if (ctx->fpscr & SZ_MASK) {
    flags |= SH4_DOUBLE_SZ;
  }

  /* translate the actual block */
  int end_flags = 0;

  for (int offset = 0; offset < block->guest_size; offset += 2) {
    uint32_t addr = block->guest_addr + offset;
    uint16_t data = guest->r16(guest->space, addr);
    union sh4_instr instr = {data};
    struct jit_opdef *def = sh4_get_opdef(data);

#if 0
    /* emit a call to the interpreter fallback for each instruction. this can
       be used to bisect and find bad ir op implementations */
    ir_fallback(ir, def->fallback, addr, data);
    end_flags = SH4_FLAG_SET_PC;
#else
    sh4_translate_cb cb = sh4_get_translator(data);
    CHECK_NOTNULL(cb);

    ir_source_info(ir, addr, offset / 2);
    cb(guest, block, ir, addr, instr, flags);

    end_flags = def->flags;
#endif

    if (def->flags & SH4_FLAG_DELAYED) {
      offset += 2;
    }
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
    ir_branch(ir, ir_alloc_i32(ir, block->guest_addr + block->guest_size));
  }

  PROF_LEAVE();
}

static void sh4_frontend_analyze_code(struct jit_frontend *base,
                                      struct jit_block *block) {
  struct sh4_frontend *frontend = (struct sh4_frontend *)base;
  struct sh4_guest *guest = (struct sh4_guest *)frontend->jit->guest;

  static int IDLE_MASK = SH4_FLAG_LOAD | SH4_FLAG_COND | SH4_FLAG_CMP;
  int idle_loop = 1;
  int all_flags = 0;
  uint32_t offset = 0;

  block->guest_size = 0;
  block->num_cycles = 0;
  block->num_instrs = 0;

  while (1) {
    uint32_t addr = block->guest_addr + offset;
    uint32_t data = guest->r16(guest->space, addr);
    union sh4_instr instr = {data};
    struct jit_opdef *def = sh4_get_opdef(data);

    offset += 2;
    block->guest_size += 2;
    block->num_cycles += def->cycles;
    block->num_instrs++;

    /* if the instruction has none of the IDLE_MASK flags, disqualify */
    idle_loop &= (def->flags & IDLE_MASK) != 0;
    all_flags |= def->flags;

    if (def->flags & SH4_FLAG_DELAYED) {
      uint32_t delay_data = guest->r16(guest->space, addr + 2);
      struct jit_opdef *delay_def = sh4_get_opdef(delay_data);

      offset += 2;
      block->guest_size += 2;
      block->num_cycles += delay_def->cycles;
      block->num_instrs++;

      /* if the instruction has none of the IDLE_MASK flags, disqualify */
      idle_loop &= (delay_def->flags & IDLE_MASK) != 0;
      all_flags |= delay_def->flags;

      /* delay slots can't have another delay slot */
      CHECK(!(delay_def->flags & SH4_FLAG_DELAYED));
    }

    /* stop emitting once a branch is hit and save off branch information */
    if (def->flags & SH4_FLAG_SET_PC) {
      if (def->op == SH4_OP_INVALID) {
        block->branch_type = JIT_BRANCH_DYNAMIC;
      } else if (def->op == SH4_OP_BF) {
        uint32_t dest_addr = ((int8_t)instr.disp_8.disp * 2) + addr + 4;
        block->branch_type = JIT_BRANCH_STATIC_FALSE;
        block->branch_addr = dest_addr;
        block->next_addr = addr + 4;
      } else if (def->op == SH4_OP_BFS) {
        uint32_t dest_addr = ((int8_t)instr.disp_8.disp * 2) + addr + 4;
        block->branch_type = JIT_BRANCH_STATIC_FALSE;
        block->branch_addr = dest_addr;
        block->next_addr = addr + 4;
      } else if (def->op == SH4_OP_BT) {
        uint32_t dest_addr = ((int8_t)instr.disp_8.disp * 2) + addr + 4;
        block->branch_type = JIT_BRANCH_STATIC_TRUE;
        block->branch_addr = dest_addr;
        block->next_addr = addr + 4;
      } else if (def->op == SH4_OP_BTS) {
        uint32_t dest_addr = ((int8_t)instr.disp_8.disp * 2) + addr + 4;
        block->branch_type = JIT_BRANCH_STATIC_TRUE;
        block->branch_addr = dest_addr;
        block->next_addr = addr + 4;
      } else if (def->op == SH4_OP_BRA) {
        /* 12-bit displacement must be sign extended */
        int32_t disp = ((instr.disp_12.disp & 0xfff) << 20) >> 20;
        uint32_t dest_addr = (disp * 2) + addr + 4;
        block->branch_type = JIT_BRANCH_STATIC;
        block->branch_addr = dest_addr;
      } else if (def->op == SH4_OP_BRAF) {
        block->branch_type = JIT_BRANCH_DYNAMIC;
      } else if (def->op == SH4_OP_BSR) {
        /* 12-bit displacement must be sign extended */
        int32_t disp = ((instr.disp_12.disp & 0xfff) << 20) >> 20;
        uint32_t ret_addr = addr + 4;
        uint32_t dest_addr = ret_addr + disp * 2;
        block->branch_type = JIT_BRANCH_STATIC;
        block->branch_addr = dest_addr;
      } else if (def->op == SH4_OP_BSRF) {
        block->branch_type = JIT_BRANCH_DYNAMIC;
      } else if (def->op == SH4_OP_JMP) {
        block->branch_type = JIT_BRANCH_DYNAMIC;
      } else if (def->op == SH4_OP_JSR) {
        block->branch_type = JIT_BRANCH_DYNAMIC;
      } else if (def->op == SH4_OP_RTS) {
        block->branch_type = JIT_BRANCH_DYNAMIC;
      } else if (def->op == SH4_OP_RTE) {
        block->branch_type = JIT_BRANCH_DYNAMIC;
      } else if (def->op == SH4_OP_TRAPA) {
        block->branch_type = JIT_BRANCH_DYNAMIC;
      } else {
        LOG_FATAL("unexpected branch op %d", def->op);
      }

      break;
    }

    /* if fpscr has changed, stop emitting since the fpu state is invalidated.
       also, if sr has changed, stop emitting as there are interrupts that
       possibly need to be handled */
    if (def->flags & (SH4_FLAG_SET_FPSCR | SH4_FLAG_SET_SR)) {
      break;
    }
  }

  /* if there was no load, disqualify */
  idle_loop &= (all_flags & SH4_FLAG_LOAD) != 0;

  /* if the branch isn't a short back edge, disqualify */
  idle_loop &= (block->guest_addr - block->branch_addr) <= 32;

  /* cheap idle skip. in an idle loop, the block is just spinning, waiting for
     an interrupt such as vblank before it'll exit. scale the block's number of
     cycles in order to yield execution faster, enabling the interrupt to
     actually be generated */
  if (idle_loop) {
#if 0
    LOG_INFO("sh4_analyze_block detected idle loop at 0x%08x",
             block->guest_addr);
#endif

    block->idle_loop = 1;
    block->num_cycles *= 10;
  }
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
  frontend->analyze_code = &sh4_frontend_analyze_code;
  frontend->translate_code = &sh4_frontend_translate_code;
  frontend->dump_code = &sh4_frontend_dump_code;
  frontend->lookup_op = &sh4_frontend_lookup_op;

  return (struct jit_frontend *)frontend;
}
