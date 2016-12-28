#include "jit/frontend/armv3/armv3_frontend.h"
#include "jit/frontend/armv3/armv3_disasm.h"
#include "jit/ir/ir.h"
#include "jit/jit.h"

static void armv3_frontend_translate_code(struct jit_frontend *base,
                                          uint32_t addr, struct ir *ir,
                                          int flags, int *size) {
  struct armv3_frontend *frontend = (struct armv3_frontend *)base;

  frontend->translate(frontend->data, addr, ir, flags, size);
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

void armv3_frontend_destroy(struct armv3_frontend *frontend) {
  free(frontend);
}

struct armv3_frontend *armv3_frontend_create(struct jit *jit) {
  struct armv3_frontend *frontend = calloc(1, sizeof(struct armv3_frontend));

  frontend->jit = jit;
  frontend->translate_code = &armv3_frontend_translate_code;
  frontend->dump_code = &armv3_frontend_dump_code;

  return frontend;
}
