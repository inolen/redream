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
                                     uint32_t begin_addr, int size,
                                     FILE *output) {
  struct armv3_frontend *frontend = (struct armv3_frontend *)base;
  struct jit_guest *guest = frontend->guest;

  int offset = 0;
  char buffer[128];

  fprintf(output, "#==--------------------------------------------------==#\n");
  fprintf(output, "# armv3\n");
  fprintf(output, "#==--------------------------------------------------==#\n");

  while (offset < size) {
    uint32_t addr = begin_addr + offset;
    uint32_t data = guest->r32(guest->mem, addr);

    armv3_format(addr, data, buffer, sizeof(buffer));
    fprintf(output, "# %s\n", buffer);

    offset += 4;
  }
}

static void armv3_frontend_translate_code(struct jit_frontend *base,
                                          uint32_t begin_addr, int size,
                                          struct ir *ir) {
  struct armv3_frontend *frontend = (struct armv3_frontend *)base;
  struct armv3_guest *guest = (struct armv3_guest *)frontend->guest;

  int offset = 0;

  while (offset < size) {
    uint32_t addr = begin_addr + offset;
    uint32_t data = guest->r32(guest->mem, addr);
    struct jit_opdef *def = armv3_get_opdef(data);

    ir_source_info(ir, addr, 12);
    ir_fallback(ir, def->fallback, addr, data);

    offset += 4;
  }
}

static void armv3_frontend_analyze_code(struct jit_frontend *base,
                                        uint32_t begin_addr, int *size) {
  struct armv3_frontend *frontend = (struct armv3_frontend *)base;
  struct armv3_guest *guest = (struct armv3_guest *)frontend->guest;

  *size = 0;

  while (1) {
    uint32_t addr = begin_addr + *size;
    uint32_t data = guest->r32(guest->mem, addr);
    union armv3_instr i = {data};
    struct jit_opdef *def = armv3_get_opdef(i.raw);

    *size += 4;

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
