#include "jit/frontend/sh4/sh4_frontend.h"
#include "jit/frontend/frontend.h"
#include "jit/frontend/sh4/sh4_analyze.h"
#include "jit/frontend/sh4/sh4_disasm.h"
#include "jit/frontend/sh4/sh4_translate.h"
#include "jit/ir/ir.h"
#include "jit/jit.h"

struct sh4_frontend {
  struct jit_frontend;
  struct jit_guest guest;
};

static void sh4_frontend_translate_code(struct jit_frontend *base,
                                        uint32_t addr, int flags, int *size,
                                        struct ir *ir) {
  struct sh4_frontend *frontend = (struct sh4_frontend *)base;

  // get the block size
  sh4_analyze_block(&frontend->guest, addr, flags, size);

  // emit IR for the SH4 code
  sh4_translate(&frontend->guest, addr, *size, flags, ir);
}

static void sh4_frontend_dump_code(struct jit_frontend *base, uint32_t addr,
                                   int size) {
  struct sh4_frontend *frontend = (struct sh4_frontend *)base;

  char buffer[128];

  int i = 0;

  while (i < size) {
    struct sh4_instr instr = {0};
    instr.addr = addr + i;
    instr.opcode = frontend->guest.r16(frontend->guest.mem_self, instr.addr);
    sh4_disasm(&instr);

    sh4_format(&instr, buffer, sizeof(buffer));
    LOG_INFO(buffer);

    i += 2;

    if (instr.flags & SH4_FLAG_DELAYED) {
      struct sh4_instr delay = {0};
      delay.addr = addr + i;
      delay.opcode = frontend->guest.r16(frontend->guest.mem_self, delay.addr);
      sh4_disasm(&delay);

      sh4_format(&delay, buffer, sizeof(buffer));
      LOG_INFO(buffer);

      i += 2;
    }
  }
}

struct jit_frontend *sh4_frontend_create(const struct jit_guest *guest) {
  struct sh4_frontend *frontend = calloc(1, sizeof(struct sh4_frontend));

  frontend->translate_code = &sh4_frontend_translate_code;
  frontend->dump_code = &sh4_frontend_dump_code;
  frontend->guest = *guest;

  return (struct jit_frontend *)frontend;
}

void sh4_frontend_destroy(struct jit_frontend *jit_frontend) {
  struct sh4_frontend *frontend = (struct sh4_frontend *)jit_frontend;

  free(frontend);
}
