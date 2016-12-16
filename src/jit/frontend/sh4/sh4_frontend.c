#include "jit/frontend/sh4/sh4_frontend.h"
#include "core/profiler.h"
#include "jit/frontend/sh4/sh4_analyze.h"
#include "jit/frontend/sh4/sh4_context.h"
#include "jit/frontend/sh4/sh4_disasm.h"
#include "jit/frontend/sh4/sh4_translate.h"
#include "jit/ir/ir.h"
#include "jit/jit.h"

static void sh4_frontend_translate_code(struct jit_frontend *base,
                                        uint32_t addr, struct ir *ir,
                                        int fastmem, int *size) {
  PROF_ENTER("cpu", "sh4_frontend_translate_code");

  struct sh4_frontend *frontend = (struct sh4_frontend *)base;
  frontend->translate(frontend->data, addr, ir, fastmem, size);

  PROF_LEAVE();
}

static void sh4_frontend_disassemble_code(struct jit_frontend *base,
                                          uint32_t addr, int size) {
  struct sh4_frontend *frontend = (struct sh4_frontend *)base;
  struct jit_guest *guest = frontend->jit->guest;

  char buffer[128];

  int i = 0;

  while (i < size) {
    struct sh4_instr instr = {0};
    instr.addr = addr + i;
    instr.opcode = guest->r16(guest->space, instr.addr);
    sh4_disasm(&instr);

    sh4_format(&instr, buffer, sizeof(buffer));
    LOG_INFO(buffer);

    i += 2;

    if (instr.flags & SH4_FLAG_DELAYED) {
      struct sh4_instr delay = {0};
      delay.addr = addr + i;
      delay.opcode = guest->r16(guest->space, delay.addr);
      sh4_disasm(&delay);

      sh4_format(&delay, buffer, sizeof(buffer));
      LOG_INFO(buffer);

      i += 2;
    }
  }
}

struct jit_frontend *sh4_frontend_create(struct jit *jit) {
  struct sh4_frontend *frontend = calloc(1, sizeof(struct sh4_frontend));

  frontend->jit = jit;
  frontend->translate_code = &sh4_frontend_translate_code;
  frontend->disassemble_code = &sh4_frontend_disassemble_code;

  return (struct jit_frontend *)frontend;
}

void sh4_frontend_destroy(struct jit_frontend *base) {
  struct sh4_frontend *frontend = (struct sh4_frontend *)base;
  free(frontend);
}
