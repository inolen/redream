#include <gflags/gflags.h>
#include "hw/machine.h"
#include "hw/debugger.h"
#include "hw/memory.h"
#include "hw/scheduler.h"

using namespace re;
using namespace re::hw;
using namespace re::ui;

DEFINE_bool(gdb, false, "Run gdb debug server");

DebugInterface::DebugInterface(Device *device) { device->debug_ = this; }

ExecuteInterface::ExecuteInterface(Device *device) : suspended_(false) {
  device->execute_ = this;
}

void ExecuteInterface::Suspend() { suspended_ = true; }

void ExecuteInterface::Resume() { suspended_ = false; }

MemoryInterface::MemoryInterface(Device *device, AddressMapper mapper)
    : mapper_(mapper), space_(*device->machine_.memory()) {
  device->memory_ = this;
}

WindowInterface::WindowInterface(Device *device) { device->window_ = this; }

Device::Device(Machine &machine, const char *name)
    : machine_(machine),
      name_(name),
      debug_(nullptr),
      execute_(nullptr),
      memory_(nullptr),
      window_(nullptr) {
  machine_.RegisterDevice(this);
}

bool Device::Init() { return true; }

Machine::Machine() : suspended_(false) {
  debugger_ = FLAGS_gdb ? new Debugger(*this) : nullptr;
  memory_ = new Memory(*this);
  scheduler_ = new Scheduler(*this);
}

Machine::~Machine() {
  delete debugger_;
  delete memory_;
  delete scheduler_;
}

bool Machine::Init() {
  if (debugger_ && !debugger_->Init()) {
    return false;
  }

  if (!memory_->Init()) {
    return false;
  }

  for (auto device : devices_) {
    if (!device->Init()) {
      return false;
    }
  }

  return true;
}

void Machine::Suspend() { suspended_ = true; }

void Machine::Resume() { suspended_ = false; }

void Machine::Tick(const std::chrono::nanoseconds &delta) {
  if (debugger_) {
    debugger_->PumpEvents();
  }

  if (!suspended_) {
    scheduler_->Tick(delta);
  }
}

Device *Machine::LookupDevice(const char *name) {
  for (auto device : devices_) {
    if (!strcmp(device->name(), name)) {
      return device;
    }
  }

  return nullptr;
}

void Machine::RegisterDevice(Device *device) { devices_.push_back(device); }

void Machine::OnPaint(bool show_main_menu) {
  for (auto device : devices_) {
    WindowInterface *window = device->window();

    if (!window) {
      continue;
    }

    window->OnPaint(show_main_menu);
  }
}

void Machine::OnKeyDown(Keycode code, int16_t value) {
  for (auto device : devices_) {
    WindowInterface *window = device->window();

    if (!window) {
      continue;
    }

    window->OnKeyDown(code, value);
  }
}
