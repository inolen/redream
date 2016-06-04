#include "jit/frontend/sh4/sh4_analyze.h"
#include "jit/frontend/sh4/sh4_disasm.h"
#include "jit/frontend/sh4/sh4_frontend.h"
#include "jit/frontend/sh4/sh4_translate.h"
#include "jit/frontend/frontend.h"
#include "jit/ir/ir.h"

typedef struct sh4_frontend_s { jit_frontend_t base; } sh4_frontend_t;

static void sh4_frontend_translate_code(sh4_frontend_t *frontend,
                                        uint32_t guest_addr, uint8_t *guest_ptr,
                                        int flags, int *size, ir_t *ir) {
  // get the block size
  sh4_analyze_block(guest_addr, guest_ptr, flags, size);

  // emit IR for the SH4 code
  sh4_translate(guest_addr, guest_ptr, *size, flags, ir);
}

static void sh4_frontend_dump_code(sh4_frontend_t *frontend,
                                   uint32_t guest_addr, uint8_t *guest_ptr,
                                   int size) {
  char buffer[128];

  int i = 0;

  while (i < size) {
    sh4_instr_t instr = {};
    instr.addr = guest_addr + i;
    instr.opcode = *(uint16_t *)(guest_ptr + i);
    sh4_disasm(&instr);

    sh4_format(&instr, buffer, sizeof(buffer));
    LOG_INFO(buffer);

    i += 2;

    if (instr.flags & SH4_FLAG_DELAYED) {
      sh4_instr_t delay = {};
      delay.addr = guest_addr + i;
      delay.opcode = *(uint16_t *)(guest_ptr + i);
      sh4_disasm(&delay);

      sh4_format(&delay, buffer, sizeof(buffer));
      LOG_INFO(buffer);

      i += 2;
    }
  }
}

jit_frontend_t *sh4_frontend_create() {
  sh4_frontend_t *frontend = calloc(1, sizeof(sh4_frontend_t));

  frontend->base.translate_code =
      (jit_frontend_translate_code)&sh4_frontend_translate_code;
  frontend->base.dump_code = (jit_frontend_dump_code)&sh4_frontend_dump_code;

  return (jit_frontend_t *)frontend;
}

void sh4_frontend_destroy(jit_frontend_t *jit_frontend) {
  sh4_frontend_t *frontend = (sh4_frontend_t *)jit_frontend;

  free(frontend);
}
