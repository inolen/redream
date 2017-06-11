#include <stdlib.h>
#include "jit/backend/jit_backend.h"
#include "jit/frontend/jit_frontend.h"
#include "jit/jit.h"

struct interp_backend {
  struct jit_backend;
};

static void interp_backend_run_code(struct jit_backend *base, int cycles) {
  struct interp_backend *backend = (struct interp_backend *)base;
  struct jit *jit = backend->jit;
  struct jit_guest *guest = jit->guest;
  void *ctx = guest->ctx;
  uint32_t *pc = ctx + guest->offset_pc;
  int32_t *run_cycles = ctx + guest->offset_cycles;
  int32_t *ran_instrs = ctx + guest->offset_instrs;

  *run_cycles = cycles;
  *ran_instrs = 0;

  while (*run_cycles > 0) {
    int RUN_SLICE = MIN(*run_cycles, 64);
    int cycles = 0;
    int instrs = 0;

    do {
      uint32_t addr = *pc;
      uint32_t data = guest->r32(guest->space, addr);
      const struct jit_opdef *def =
          jit->frontend->lookup_op(jit->frontend, &data);
      def->fallback(guest, addr, data);
      cycles += def->cycles;
      instrs += 1;
    } while (cycles < RUN_SLICE);

    *run_cycles -= cycles;
    *ran_instrs += instrs;

    guest->interrupt_check(guest->data);
  }
}

static int interp_backend_handle_exception(struct jit_backend *base,
                                           struct exception_state *ex) {
  return 0;
}

static void interp_backend_dump_code(struct jit_backend *base,
                                     const uint8_t *code, int size) {}

static void interp_backend_reset(struct jit_backend *base) {}

static void interp_backend_destroy(struct jit_backend *base) {
  struct interp_backend *backend = (struct interp_backend *)base;

  free(backend);
}

static void interp_backend_init(struct jit_backend *base) {
  struct interp_backend *backend = (struct interp_backend *)base;
}

struct jit_backend *interp_backend_create() {
  struct interp_backend *backend = calloc(1, sizeof(struct interp_backend));

  backend->init = &interp_backend_init;
  backend->destroy = &interp_backend_destroy;

  /* compile interface */
  backend->registers = NULL;
  backend->num_registers = 0;
  backend->reset = &interp_backend_reset;
  backend->assemble_code = NULL;
  backend->dump_code = &interp_backend_dump_code;
  backend->handle_exception = &interp_backend_handle_exception;

  /* dispatch interface */
  backend->run_code = &interp_backend_run_code;
  backend->lookup_code = NULL;
  backend->cache_code = NULL;
  backend->invalidate_code = NULL;
  backend->patch_edge = NULL;
  backend->restore_edge = NULL;

  return (struct jit_backend *)backend;
}
