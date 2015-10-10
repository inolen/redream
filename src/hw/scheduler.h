#ifndef SCHEDULER_H
#define SCHEDULER_H

#include <chrono>
#include <vector>

namespace dreavm {
namespace hw {

enum { INVALID_HANDLE = -1, NS_PER_SEC = 1000000000 };

static inline std::chrono::nanoseconds HZ_TO_NANO(int64_t hz) {
  return std::chrono::nanoseconds(NS_PER_SEC / hz);
}

static inline std::chrono::nanoseconds MHZ_TO_NANO(int64_t hz) {
  return std::chrono::nanoseconds(NS_PER_SEC / (hz * 1000000));
}

class Device {
 public:
  virtual ~Device(){};

  virtual int GetClockFrequency() = 0;
  virtual int Run(int cycles) = 0;
};

struct DeviceInfo {
  DeviceInfo(Device *device) : device(device), current_time() {}

  Device *device;
  std::chrono::high_resolution_clock::time_point current_time;
};

typedef int DeviceHandle;

class Scheduler {
 public:
  Scheduler();

  DeviceHandle AddDevice(Device *device);
  void Tick(const std::chrono::nanoseconds &delta);

 private:
  std::vector<DeviceInfo> devices_;

  std::chrono::high_resolution_clock::time_point base_time_;
};
}
}

#endif
