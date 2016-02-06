#ifndef SCHEDULER_H
#define SCHEDULER_H

#include <chrono>
#include <functional>

namespace re {
namespace hw {

enum {
  MAX_TIMERS = 16,
};

typedef int64_t TimerHandle;

enum : TimerHandle {
  INVALID_TIMER = -1,
};

typedef std::function<void(const std::chrono::nanoseconds &)> TimerCallback;

struct Timer {
  TimerHandle handle;
  std::chrono::nanoseconds period;
  std::chrono::high_resolution_clock::time_point expire;
  TimerCallback callback;
};

enum : int64_t {
  NS_PER_SEC = 1000000000ll,
};

static inline std::chrono::nanoseconds HZ_TO_NANO(int64_t hz) {
  return std::chrono::nanoseconds(
      static_cast<int64_t>(NS_PER_SEC / static_cast<float>(hz)));
}

static inline int64_t NANO_TO_CYCLES(const std::chrono::nanoseconds &ns,
                                     int64_t hz) {
  return static_cast<int64_t>((ns.count() / static_cast<float>(NS_PER_SEC)) *
                              hz);
}

static inline std::chrono::nanoseconds CYCLES_TO_NANO(int64_t cycles,
                                                      int64_t hz) {
  return std::chrono::nanoseconds(
      static_cast<int64_t>((cycles / static_cast<float>(hz)) * NS_PER_SEC));
}

class Scheduler {
 public:
  Scheduler();

  std::chrono::high_resolution_clock::time_point base_time() {
    return base_time_;
  }

  void Tick(const std::chrono::nanoseconds &delta);

  TimerHandle AddTimer(const std::chrono::nanoseconds &period,
                       TimerCallback callback);
  Timer &GetTimer(TimerHandle handle);
  bool RemoveTimer(TimerHandle handle);

 private:
  std::chrono::high_resolution_clock::time_point base_time_;
  Timer timers_[MAX_TIMERS];
  int num_timers_;
  TimerHandle next_handle_;
};
}
}

#endif
