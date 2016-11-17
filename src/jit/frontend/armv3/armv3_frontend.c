#include "jit/frontend/armv3/armv3_frontend.h"
#include "jit/frontend/armv3/armv3_analyze.h"
#include "jit/frontend/armv3/armv3_disasm.h"
#include "jit/frontend/armv3/armv3_translate.h"
#include "jit/frontend/frontend.h"
#include "jit/ir/ir.h"
#include "jit/jit.h"

struct armv3_frontend {
  struct jit_frontend;
  const struct armv3_guest *guest;
};

static void armv3_frontend_translate_code(struct jit_frontend *base,
                                          uint32_t addr, int flags, int *size,
                                          struct ir *ir) {
  struct armv3_frontend *frontend = (struct armv3_frontend *)base;

  // get the block flags / size
  armv3_analyze_block(frontend->guest, addr, &flags, size);

  // emit ir for the arm7 code
  armv3_translate(frontend->guest, addr, *size, flags, ir);
}

static void armv3_frontend_dump_code(struct jit_frontend *base, uint32_t addr,
                                     int size) {
  struct armv3_frontend *frontend = (struct armv3_frontend *)base;

  char buffer[128];

  for (int i = 0; i < size; i += 4) {
    uint32_t data = frontend->guest->r32(frontend->guest->mem_self, addr);

    armv3_format(addr, data, buffer, sizeof(buffer));
    LOG_INFO(buffer);

    addr += 4;
  }
}

struct jit_frontend *armv3_frontend_create(const struct armv3_guest *guest) {
  struct armv3_frontend *frontend = calloc(1, sizeof(struct armv3_frontend));

  frontend->translate_code = &armv3_frontend_translate_code;
  frontend->dump_code = &armv3_frontend_dump_code;
  frontend->guest = guest;

  return (struct jit_frontend *)frontend;
}

void armv3_frontend_destroy(struct jit_frontend *jit_frontend) {
  struct armv3_frontend *frontend = (struct armv3_frontend *)jit_frontend;

  free(frontend);
}
