#include "gdb/gdb_server.h"
#define GDB_SERVER_IMPL
#include "gdb/gdb_server.h"

#include "core/log.h"
#include "hw/debugger.h"
#include "hw/dreamcast.h"

struct debugger {
  struct dreamcast *dc;
  struct device *dev;
  gdb_server_t *sv;
};

static void debugger_gdb_server_detach(void *data) {
  struct debugger *dbg = data;
  dc_resume(dbg->dc);
}

static void debugger_gdb_server_stop(void *data) {
  struct debugger *dbg = data;
  dc_suspend(dbg->dc);
}

static void debugger_gdb_server_resume(void *data) {
  struct debugger *dbg = data;
  dc_resume(dbg->dc);
}

static void debugger_gdb_server_step(void *data) {
  struct debugger *dbg = data;
  dbg->dev->debug->step(dbg->dev);
}

static void debugger_gdb_server_add_bp(void *data, int type, intmax_t addr) {
  struct debugger *dbg = data;
  dbg->dev->debug->add_bp(dbg->dev, type, (uint32_t)addr);
}

static void debugger_gdb_server_rem_bp(void *data, int type, intmax_t addr) {
  struct debugger *dbg = data;
  dbg->dev->debug->rem_bp(dbg->dev, type, (uint32_t)addr);
}

static void debugger_gdb_server_read_mem(void *data, intmax_t addr,
                                         uint8_t *buffer, int size) {
  struct debugger *dbg = data;
  dbg->dev->debug->read_mem(dbg->dev, (uint32_t)addr, buffer, size);
}

static void debugger_gdb_server_read_reg(void *data, int n, intmax_t *value,
                                         int *size) {
  struct debugger *dbg = data;
  uint64_t v = 0;
  dbg->dev->debug->read_reg(dbg->dev, n, &v, size);
  *value = v;
}

bool debugger_init(struct debugger *dbg) {
  // use the first device found with a debug interface
  list_for_each_entry(dev, &dbg->dc->devices, struct device, it) {
    if (dev->debug) {
      dbg->dev = dev;
      break;
    }
  }

  // didn't find a debuggable device
  if (!dbg->dev) {
    return false;
  }

  // create the gdb server
  gdb_target_t target;
  target.ctx = dbg;
  target.endian = GDB_LITTLE_ENDIAN;
  target.num_regs = dbg->dev->debug->num_regs(dbg->dev);
  target.detach = &debugger_gdb_server_detach;
  target.stop = &debugger_gdb_server_stop;
  target.resume = &debugger_gdb_server_resume;
  target.step = &debugger_gdb_server_step;
  target.add_bp = &debugger_gdb_server_add_bp;
  target.rem_bp = &debugger_gdb_server_rem_bp;
  target.read_reg = &debugger_gdb_server_read_reg;
  target.read_mem = &debugger_gdb_server_read_mem;

  dbg->sv = gdb_server_create(&target, 24690);
  if (!dbg->sv) {
    LOG_WARNING("Failed to create GDB server");
    return false;
  }

  return true;
}

void debugger_trap(struct debugger *dbg) {
  gdb_server_interrupt(dbg->sv, GDB_SIGNAL_TRAP);

  dc_suspend(dbg->dc);
}

void debugger_tick(struct debugger *dbg) {
  gdb_server_pump(dbg->sv);
}

struct debugger *debugger_create(struct dreamcast *dc) {
  struct debugger *dbg = calloc(1, sizeof(struct debugger));

  dbg->dc = dc;

  return NULL;
}

void debugger_destroy(struct debugger *dbg) {
  gdb_server_destroy(dbg->sv);
  free(dbg);
}
