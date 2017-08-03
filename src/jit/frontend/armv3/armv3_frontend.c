#include "jit/frontend/armv3/armv3_frontend.h"
#include "jit/frontend/armv3/armv3_context.h"
#include "jit/frontend/armv3/armv3_disasm.h"
#include "jit/frontend/armv3/armv3_fallback.h"
#include "jit/frontend/armv3/armv3_guest.h"
#include "jit/ir/ir.h"
#include "jit/jit.h"
#include "jit/jit_guest.h"

struct armv3_frontend {
  struct jit_frontend;
};

static const struct jit_opdef *armv3_frontend_lookup_op(
    struct jit_frontend *base, const void *instr) {
  return armv3_get_opdef(*(const uint32_t *)instr);
}

static void armv3_frontend_dump_code(struct jit_frontend *base,
                                     const struct jit_block *block,
                                     FILE *output) {
  struct armv3_frontend *frontend = (struct armv3_frontend *)base;
  struct jit_guest *guest = frontend->guest;

  char buffer[128];

  for (int offset = 0; offset < block->guest_size; offset += 4) {
    uint32_t addr = block->guest_addr + offset;
    uint32_t data = guest->r32(guest->space, addr);

    armv3_format(addr, data, buffer, sizeof(buffer));
    fprintf(output, "# %s\n", buffer);

    addr += 4;
  }
}

static void armv3_frontend_translate_code(struct jit_frontend *base,
                                          struct jit_block *block,
                                          struct ir *ir) {
  struct armv3_frontend *frontend = (struct armv3_frontend *)base;
  struct armv3_guest *guest = (struct armv3_guest *)frontend->guest;

  for (int offset = 0; offset < block->guest_size; offset += 4) {
    uint32_t addr = block->guest_addr + offset;
    uint32_t data = guest->r32(guest->space, addr);
    struct jit_opdef *def = armv3_get_opdef(data);

    ir_source_info(ir, addr);
    ir_fallback(ir, def->fallback, addr, data);
  }
}

static void armv3_frontend_analyze_code(struct jit_frontend *base,
                                        struct jit_block *block) {
  struct armv3_frontend *frontend = (struct armv3_frontend *)base;
  struct armv3_guest *guest = (struct armv3_guest *)frontend->guest;
  uint32_t addr = block->guest_addr;

  block->guest_size = 0;
  block->num_cycles = 0;
  block->num_instrs = 0;

  while (1) {
    uint32_t data = guest->r32(guest->space, addr);
    union armv3_instr i = {data};
    struct jit_opdef *def = armv3_get_opdef(i.raw);

    addr += 4;
    block->guest_size += 4;
    block->num_cycles += 12;
    block->num_instrs++;

    /* stop emitting when pc is changed */
    int mov_to_pc = 0;

    mov_to_pc |= (def->flags & FLAG_SET_PC);
    mov_to_pc |= (def->flags & FLAG_DATA) && i.data.rd == 15;
    mov_to_pc |= (def->flags & FLAG_PSR);
    mov_to_pc |= (def->flags & FLAG_XFR) && i.xfr.rd == 15;
    mov_to_pc |= (def->flags & FLAG_BLK) && (i.blk.rlist & (1 << 15));
    mov_to_pc |= (def->flags & FLAG_SWI);

    if (mov_to_pc) {
      break;
    }
  }
}

void armv3_frontend_destroy(struct jit_frontend *base) {
  struct armv3_frontend *frontend = (struct armv3_frontend *)base;

  free(frontend);
}

struct jit_frontend *armv3_frontend_create(struct jit_guest *guest) {
  struct armv3_frontend *frontend = calloc(1, sizeof(struct armv3_frontend));

  frontend->guest = guest;
  frontend->destroy = &armv3_frontend_destroy;
  frontend->analyze_code = &armv3_frontend_analyze_code;
  frontend->translate_code = &armv3_frontend_translate_code;
  frontend->dump_code = &armv3_frontend_dump_code;
  frontend->lookup_op = &armv3_frontend_lookup_op;

  return (struct jit_frontend *)frontend;
}
