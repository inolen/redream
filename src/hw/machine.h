#ifndef MACHINE_H
#define MACHINE_H

#include <chrono>
#include <vector>
#include "hw/memory.h"
#include "ui/window.h"

namespace re {
namespace hw {

class Device;
class Debugger;
class Machine;
class Memory;
class Scheduler;

class DebugInterface {
 public:
  DebugInterface(Device *device);
  virtual ~DebugInterface() = default;

  virtual int NumRegisters() = 0;

  virtual void Step() = 0;

  virtual void AddBreakpoint(int type, uint32_t addr) = 0;
  virtual void RemoveBreakpoint(int type, uint32_t addr) = 0;

  virtual void ReadMemory(uint32_t addr, uint8_t *buffer, int size) = 0;
  virtual void ReadRegister(int n, uint64_t *value, int *size) = 0;
};

class ExecuteInterface {
 public:
  ExecuteInterface(Device *device);
  virtual ~ExecuteInterface() = default;

  bool suspended() { return suspended_; }

  void Suspend();
  void Resume();

  virtual void Run(const std::chrono::nanoseconds &delta) = 0;

 private:
  bool suspended_;
};

class MemoryInterface {
 public:
  MemoryInterface(Device *device, AddressMapper mapper);
  virtual ~MemoryInterface() = default;

  AddressMapper mapper() { return mapper_; }
  AddressSpace &space() { return space_; }

 protected:
  AddressMapper mapper_;
  AddressSpace space_;
};

class WindowInterface {
 public:
  WindowInterface(Device *device);
  virtual ~WindowInterface() = default;

  virtual void OnPaint(bool show_main_menu){};
  virtual void OnKeyDown(ui::Keycode code, int16_t value){};
};

class Device {
  friend class DebugInterface;
  friend class ExecuteInterface;
  friend class MemoryInterface;
  friend class WindowInterface;

 public:
  Device(Machine &machine, const char *name);
  virtual ~Device() = default;

  const char *name() { return name_; }
  DebugInterface *debug() { return debug_; }
  ExecuteInterface *execute() { return execute_; }
  MemoryInterface *memory() { return memory_; }
  WindowInterface *window() { return window_; }

  virtual bool Init();

 private:
  Machine &machine_;
  const char *name_;
  DebugInterface *debug_;
  ExecuteInterface *execute_;
  MemoryInterface *memory_;
  WindowInterface *window_;
};

class Machine {
  friend class Device;

 public:
  Machine();
  virtual ~Machine();

  bool suspended() { return suspended_; }
  Debugger *debugger() { return debugger_; }
  Memory *memory() { return memory_; }
  Scheduler *scheduler() { return scheduler_; }
  std::vector<Device *> &devices() { return devices_; }

  bool Init();
  void Suspend();
  void Resume();

  Device *LookupDevice(const char *name);
  void RegisterDevice(Device *device);

  void Tick(const std::chrono::nanoseconds &delta);
  void OnPaint(bool show_main_menu);
  void OnKeyDown(ui::Keycode code, int16_t value);

 private:
  bool suspended_;
  Debugger *debugger_;
  Memory *memory_;
  Scheduler *scheduler_;
  std::vector<Device *> devices_;
};
}
}

#endif
