#ifndef MACHINE_H
#define MACHINE_H

#include <chrono>
#include <vector>
#include "ui/window.h"

namespace re {
namespace hw {

class Device;
class Debugger;
class Machine;
class Memory;
class MemoryMap;
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

  virtual void Run(const std::chrono::nanoseconds &delta) = 0;
};

class MemoryInterface {
 public:
  MemoryInterface(Device *device);
  virtual ~MemoryInterface() = default;

  virtual void MapPhysicalMemory(Memory &memory, MemoryMap &memmap) {}
  virtual void MapVirtualMemory(Memory &memory, MemoryMap &memmap) {}
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
  virtual ~Device() = default;

  DebugInterface *debug() { return debug_; }
  ExecuteInterface *execute() { return execute_; }
  MemoryInterface *memory() { return memory_; }
  WindowInterface *window() { return window_; }

  Device(Machine &machine);

  virtual bool Init();

 private:
  DebugInterface *debug_;
  ExecuteInterface *execute_;
  MemoryInterface *memory_;
  WindowInterface *window_;
};

class Machine {
  friend class Device;

 public:
  bool suspended() { return suspended_; }

  Machine();
  virtual ~Machine();

  bool Init();
  void Suspend();
  void Resume();

  void Tick(const std::chrono::nanoseconds &delta);
  void OnPaint(bool show_main_menu);
  void OnKeyDown(ui::Keycode code, int16_t value);

  Debugger *debugger;
  Memory *memory;
  Scheduler *scheduler;
  std::vector<Device *> devices;

 private:
  bool suspended_;
};
}
}

#endif
