#include <gflags/gflags.h>
#include "hw/machine.h"
#include "hw/debugger.h"
#include "hw/memory.h"
#include "hw/scheduler.h"

using namespace re;
using namespace re::hw;

DEFINE_bool(debug, false, "Run debug server");

DebugInterface::DebugInterface(Device *device) { device->debug_ = this; }

ExecuteInterface::ExecuteInterface(Device *device) { device->execute_ = this; }

MemoryInterface::MemoryInterface(Device *device) { device->memory_ = this; }

Device::Device(Machine &machine)
    : debug_(nullptr), execute_(nullptr), memory_(nullptr) {
  machine.devices.push_back(this);
}

bool Device::Init() { return true; }

Machine::Machine() : suspended_(false) {
  debugger = FLAGS_debug ? new Debugger(*this) : nullptr;
  memory = new Memory(*this);
  scheduler = new Scheduler(*this);
}

Machine::~Machine() {
  delete debugger;
  delete memory;
  delete scheduler;
}

bool Machine::Init() {
  if (debugger && !debugger->Init()) {
    return false;
  }

  if (!memory->Init()) {
    return false;
  }

  for (auto device : devices) {
    if (!device->Init()) {
      return false;
    }
  }

  return true;
}

void Machine::Suspend() { suspended_ = true; }

void Machine::Resume() { suspended_ = false; }

void Machine::Tick(const std::chrono::nanoseconds &delta) {
  if (debugger) {
    debugger->PumpEvents();
  }

  if (!suspended_) {
    scheduler->Tick(delta);
  }
}
