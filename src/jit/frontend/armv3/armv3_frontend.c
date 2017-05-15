#include "jit/frontend/armv3/armv3_frontend.h"
#include "jit/frontend/armv3/armv3_context.h"
#include "jit/frontend/armv3/armv3_disasm.h"
#include "jit/frontend/armv3/armv3_fallback.h"
#include "jit/frontend/armv3/armv3_guest.h"
#include "jit/ir/ir.h"
#include "jit/jit.h"

struct armv3_frontend {
  struct jit_frontend;
};

static void armv3_analyze_block(const struct armv3_guest *guest,
                                struct jit_block *block) {
  uint32_t addr = block->guest_addr;

  block->guest_size = 0;
  block->num_cycles = 0;
  block->num_instrs = 0;

  while (1) {
    uint32_t data = guest->r32(guest->space, addr);
    union armv3_instr i = {data};
    struct armv3_desc *desc = armv3_get_opdesc(i.raw);

    addr += 4;
    block->guest_size += 4;
    block->num_cycles += 12;
    block->num_instrs++;

    /* end block on invalid instruction */
    if (desc->op == ARMV3_OP_INVALID) {
      break;
    }

    /* stop emitting when pc is changed */
    if ((desc->flags & FLAG_BRANCH) ||
        ((desc->flags & FLAG_DATA) && i.data.rd == 15) ||
        (desc->flags & FLAG_PSR) ||
        ((desc->flags & FLAG_XFR) && i.xfr.rd == 15) ||
        ((desc->flags & FLAG_BLK) && i.blk.rlist & (1 << 15)) ||
        (desc->flags & FLAG_SWI)) {
      break;
    }
  }
}

static void armv3_frontend_translate_code(struct jit_frontend *base,
                                          struct jit_block *block,
                                          struct ir *ir) {
  struct armv3_frontend *frontend = (struct armv3_frontend *)base;
  struct armv3_guest *guest = (struct armv3_guest *)frontend->jit->guest;

  armv3_analyze_block(guest, block);

  /* emit fallbacks */
  uint32_t addr = block->guest_addr;
  uint32_t end = block->guest_addr + block->guest_size;

  while (addr < end) {
    uint32_t instr = guest->r32(guest->space, addr);

    armv3_fallback_cb fallback = armv3_get_fallback(instr);
    ir_fallback(ir, fallback, addr, instr);

    addr += 4;
  }

  /* branch to the current pc */
  struct ir_value *pc =
      ir_load_context(ir, offsetof(struct armv3_context, r[15]), VALUE_I32);
  ir_branch(ir, pc);
}

static void armv3_frontend_dump_code(struct jit_frontend *base, uint32_t addr,
                                     int size) {
  struct armv3_frontend *frontend = (struct armv3_frontend *)base;
  struct jit_guest *guest = frontend->jit->guest;

  char buffer[128];

  for (int i = 0; i < size; i += 4) {
    uint32_t data = guest->r32(guest->space, addr);

    armv3_format(addr, data, buffer, sizeof(buffer));
    LOG_INFO(buffer);

    addr += 4;
  }
}

static void armv3_frontend_init(struct jit_frontend *frontend) {}

void armv3_frontend_destroy(struct armv3_frontend *frontend) {
  free(frontend);
}

struct armv3_frontend *armv3_frontend_create(struct jit *jit) {
  struct armv3_frontend *frontend = calloc(1, sizeof(struct armv3_frontend));

  frontend->jit = jit;
  frontend->init = &armv3_frontend_init;
  frontend->translate_code = &armv3_frontend_translate_code;
  frontend->dump_code = &armv3_frontend_dump_code;

  return frontend;
}
