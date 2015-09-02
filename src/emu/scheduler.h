#ifndef SCHEDULER_H
#define SCHEDULER_H

#include <chrono>
#include <functional>
#include <set>
#include <vector>
#include "emu/device.h"

namespace dreavm {
namespace emu {

typedef std::function<void()> TimerCallback;

enum { INVALID_HANDLE = -1, NS_PER_SEC = 1000000000, NS_PER_MS = 1000000 };

static inline std::chrono::nanoseconds HZ_TO_NANO(int64_t hz) {
  return std::chrono::nanoseconds(NS_PER_SEC / hz);
}

static inline std::chrono::nanoseconds MHZ_TO_NANO(int64_t hz) {
  return std::chrono::nanoseconds(NS_PER_SEC / (hz * 1000000));
}

struct DeviceInfo {
  DeviceInfo(Device *device) : device(device), current_time() {}

  Device *device;
  std::chrono::high_resolution_clock::time_point current_time;
};

typedef int DeviceHandle;
typedef int TimerHandle;

struct Timer {
 public:
  Timer(TimerHandle handle, std::chrono::nanoseconds period,
        std::chrono::high_resolution_clock::time_point expire,
        TimerCallback callback)
      : handle(handle), period(period), expire(expire), callback(callback) {}

  bool operator<(const Timer &rhs) const { return expire < rhs.expire; }

  TimerHandle handle;
  std::chrono::nanoseconds period;
  std::chrono::high_resolution_clock::time_point expire;
  TimerCallback callback;
};

class Scheduler {
 public:
  Scheduler();

  DeviceHandle AddDevice(Device *device);
  TimerHandle AddTimer(const std::chrono::nanoseconds &period,
                       TimerCallback callback);
  void AdjustTimer(TimerHandle handle, const std::chrono::nanoseconds &period);
  void RemoveTimer(TimerHandle handle);
  void Tick(const std::chrono::nanoseconds &delta);

 private:
  std::vector<DeviceInfo> devices_;
  std::set<Timer> timers_;

  TimerHandle next_timer_handle_;
  std::chrono::high_resolution_clock::time_point base_time_;
};
}
}

#endif
