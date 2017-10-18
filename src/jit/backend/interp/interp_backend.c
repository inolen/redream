#include <stdlib.h>
#include "jit/jit.h"
#include "jit/jit_backend.h"
#include "jit/jit_frontend.h"
#include "jit/jit_guest.h"

struct interp_backend {
  struct jit_backend;

  /* used to resolve the fallback handler for each instruction */
  struct jit_frontend *frontend;
};

static void interp_backend_run_code(struct jit_backend *base, int cycles) {
  struct interp_backend *backend = (struct interp_backend *)base;
  struct jit_frontend *frontend = backend->frontend;
  struct jit_guest *guest = backend->guest;
  uint8_t *ctx = guest->ctx;
  uint32_t *pc = (uint32_t *)(ctx + guest->offset_pc);
  int32_t *run_cycles = (int32_t *)(ctx + guest->offset_cycles);
  int32_t *ran_instrs = (int32_t *)(ctx + guest->offset_instrs);

  *run_cycles = cycles;
  *ran_instrs = 0;

  while (*run_cycles > 0) {
    int RUN_SLICE = MIN(*run_cycles, 64);
    int cycles = 0;
    int instrs = 0;

    do {
      uint32_t addr = *pc;
      uint32_t data = guest->r32(guest->mem, addr);
      const struct jit_opdef *def = frontend->lookup_op(frontend, &data);
      def->fallback(guest, addr, data);
      cycles += def->cycles;
      instrs += 1;
    } while (cycles < RUN_SLICE);

    *run_cycles -= cycles;
    *ran_instrs += instrs;

    guest->check_interrupts(guest->data);
  }
}

static int interp_backend_handle_exception(struct jit_backend *base,
                                           struct exception_state *ex) {
  return 0;
}

static void interp_backend_dump_code(struct jit_backend *base,
                                     const uint8_t *addr, int size,
                                     FILE *output) {}

static void interp_backend_reset(struct jit_backend *base) {}

static void interp_backend_destroy(struct jit_backend *base) {
  struct interp_backend *backend = (struct interp_backend *)base;

  free(backend);
}

struct jit_backend *interp_backend_create(struct jit_guest *guest,
                                          struct jit_frontend *frontend) {
  struct interp_backend *backend = calloc(1, sizeof(struct interp_backend));

  backend->guest = guest;
  backend->frontend = frontend;
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
