#include "jit/frontend/sh4/sh4_analyze.h"
#include "jit/frontend/sh4/sh4_disasm.h"
#include "jit/frontend/sh4/sh4_frontend.h"
#include "jit/frontend/sh4/sh4_translate.h"
#include "jit/frontend/frontend.h"
#include "jit/ir/ir.h"

struct sh4_frontend {
  struct jit_frontend base;
};

static void sh4_frontend_translate_code(struct jit_frontend *base,
                                        uint32_t guest_addr, uint8_t *guest_ptr,
                                        int flags, int *size, struct ir *ir) {
  struct sh4_frontend *frontend = container_of(base, struct sh4_frontend, base);

  // get the block size
  sh4_analyze_block(guest_addr, guest_ptr, flags, size);

  // emit IR for the SH4 code
  sh4_translate(guest_addr, guest_ptr, *size, flags, ir);
}

static void sh4_frontend_dump_code(struct jit_frontend *base,
                                   uint32_t guest_addr, uint8_t *guest_ptr,
                                   int size) {
  struct sh4_frontend *frontend = container_of(base, struct sh4_frontend, base);

  char buffer[128];

  int i = 0;

  while (i < size) {
    struct sh4_instr instr = {};
    instr.addr = guest_addr + i;
    instr.opcode = *(uint16_t *)(guest_ptr + i);
    sh4_disasm(&instr);

    sh4_format(&instr, buffer, sizeof(buffer));
    LOG_INFO(buffer);

    i += 2;

    if (instr.flags & SH4_FLAG_DELAYED) {
      struct sh4_instr delay = {};
      delay.addr = guest_addr + i;
      delay.opcode = *(uint16_t *)(guest_ptr + i);
      sh4_disasm(&delay);

      sh4_format(&delay, buffer, sizeof(buffer));
      LOG_INFO(buffer);

      i += 2;
    }
  }
}

struct jit_frontend *sh4_frontend_create() {
  struct sh4_frontend *frontend = calloc(1, sizeof(struct sh4_frontend));

  frontend->base.translate_code = &sh4_frontend_translate_code;
  frontend->base.dump_code = &sh4_frontend_dump_code;

  return (struct jit_frontend *)frontend;
}

void sh4_frontend_destroy(struct jit_frontend *jit_frontend) {
  struct sh4_frontend *frontend = (struct sh4_frontend *)jit_frontend;

  free(frontend);
}
