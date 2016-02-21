#include "hw/machine.h"
#include "hw/memory.h"
#include "hw/scheduler.h"

using namespace re;
using namespace re::hw;

ExecuteInterface::ExecuteInterface(Device *device) { device->execute_ = this; }

MemoryInterface::MemoryInterface(Device *device) { device->memory_ = this; }

Device::Device(Machine &machine) : execute_(nullptr), memory_(nullptr) {
  machine.devices.push_back(this);
}

Machine::Machine() {
  memory = new Memory(*this);
  scheduler = new Scheduler(*this);
}

Machine::~Machine() {
  delete memory;
  delete scheduler;
}

bool Machine::Init() {
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
