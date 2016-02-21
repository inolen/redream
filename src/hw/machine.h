#ifndef MACHINE_H
#define MACHINE_H

#include <chrono>
#include <vector>

namespace re {
namespace hw {

class Device;
class Machine;
class Memory;
class MemoryMap;
class Scheduler;

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

class Device {
  friend class ExecuteInterface;
  friend class MemoryInterface;

 public:
  virtual ~Device() = default;

  ExecuteInterface *execute() { return execute_; }
  MemoryInterface *memory() { return memory_; }

  Device(Machine &machine);

  virtual bool Init() { return true; }

 private:
  ExecuteInterface *execute_;
  MemoryInterface *memory_;
};

class Machine {
  friend class Device;

 public:
  Machine();
  virtual ~Machine();

  bool Init();

  Memory *memory;
  Scheduler *scheduler;
  std::vector<Device *> devices;
};
}
}

#endif
