#define GDB_SERVER_IMPL
#include "gdb/gdb_server.h"

#include "core/log.h"
#include "hw/debugger.h"
#include "hw/machine.h"
#include "hw/memory.h"

using namespace re;
using namespace re::hw;

Debugger::Debugger(Machine &machine) : machine_(machine), sv_(nullptr) {}

Debugger::~Debugger() { gdb_server_destroy(sv_); }

bool Debugger::Init() {
  // use the first device found with a debug interface
  for (auto device : machine_.devices) {
    debug_ = device->debug();

    if (debug_) {
      break;
    }
  }

  // didn't find a debuggable device
  if (!debug_) {
    return false;
  }

  // create the gdb server
  gdb_target_t target;
  target.ctx = this;
  target.endian = GDB_LITTLE_ENDIAN;
  target.num_regs = debug_->NumRegisters();
  target.detach = &Debugger::gdb_server_detach;
  target.stop = &Debugger::gdb_server_stop;
  target.resume = &Debugger::gdb_server_resume;
  target.step = &Debugger::gdb_server_step;
  target.add_bp = &Debugger::gdb_server_add_bp;
  target.rem_bp = &Debugger::gdb_server_rem_bp;
  target.read_reg = &Debugger::gdb_server_read_reg;
  target.read_mem = &Debugger::gdb_server_read_mem;

  sv_ = gdb_server_create(&target, 24690);
  if (!sv_) {
    LOG_WARNING("Failed to create GDB server");
    return false;
  }

  return true;
}

void Debugger::Trap() {
  gdb_server_interrupt(sv_, GDB_SIGNAL_TRAP);

  machine_.Suspend();
}

void Debugger::PumpEvents() { gdb_server_pump(sv_); }

void Debugger::gdb_server_detach(void *data) {
  Debugger *debugger = reinterpret_cast<Debugger *>(data);
  debugger->machine_.Resume();
}

void Debugger::gdb_server_stop(void *data) {
  Debugger *debugger = reinterpret_cast<Debugger *>(data);
  debugger->machine_.Suspend();
}

void Debugger::gdb_server_resume(void *data) {
  Debugger *debugger = reinterpret_cast<Debugger *>(data);
  debugger->machine_.Resume();
}

void Debugger::gdb_server_step(void *data) {
  Debugger *debugger = reinterpret_cast<Debugger *>(data);
  debugger->debug_->Step();
}

void Debugger::gdb_server_add_bp(void *data, int type, intmax_t addr) {
  Debugger *debugger = reinterpret_cast<Debugger *>(data);
  debugger->debug_->AddBreakpoint(type, static_cast<uint32_t>(addr));
}

void Debugger::gdb_server_rem_bp(void *data, int type, intmax_t addr) {
  Debugger *debugger = reinterpret_cast<Debugger *>(data);
  debugger->debug_->RemoveBreakpoint(type, static_cast<uint32_t>(addr));
}

void Debugger::gdb_server_read_mem(void *data, intmax_t addr, uint8_t *buffer,
                                   int size) {
  Debugger *debugger = reinterpret_cast<Debugger *>(data);
  debugger->debug_->ReadMemory(static_cast<uint32_t>(addr), buffer, size);
}

void Debugger::gdb_server_read_reg(void *data, int n, intmax_t *value,
                                   int *size) {
  Debugger *debugger = reinterpret_cast<Debugger *>(data);
  uint64_t v = 0;
  debugger->debug_->ReadRegister(n, &v, size);
  *value = v;
}
