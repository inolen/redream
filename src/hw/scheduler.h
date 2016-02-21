#ifndef SCHEDULER_H
#define SCHEDULER_H

#include <stdint.h>
#include <chrono>
#include "core/arena.h"
#include "core/delegate.h"
#include "core/intrusive_list.h"

namespace re {
namespace hw {

class Machine;

typedef re::delegate<void()> TimerDelegate;

struct Timer : public IntrusiveListNode<Timer> {
  std::chrono::high_resolution_clock::time_point expire;
  TimerDelegate delegate;
};

typedef Timer *TimerHandle;
static const TimerHandle INVALID_TIMER = nullptr;
static const int64_t NS_PER_SEC = 1000000000ll;

static inline std::chrono::nanoseconds HZ_TO_NANO(int64_t hz) {
  float nano = NS_PER_SEC / static_cast<float>(hz);
  return std::chrono::nanoseconds(static_cast<int64_t>(nano));
}

static inline int64_t NANO_TO_CYCLES(const std::chrono::nanoseconds &ns,
                                     int64_t hz) {
  float cycles = (ns.count() / static_cast<float>(NS_PER_SEC)) * hz;
  return static_cast<int64_t>(cycles);
}

static inline std::chrono::nanoseconds CYCLES_TO_NANO(int64_t cycles,
                                                      int64_t hz) {
  float nano = (cycles / static_cast<float>(hz)) * NS_PER_SEC;
  return std::chrono::nanoseconds(static_cast<int64_t>(nano));
}

class Scheduler {
 public:
  Scheduler(Machine &machine);

  void Tick(const std::chrono::nanoseconds &delta);

  TimerHandle ScheduleTimer(TimerDelegate delegate,
                            const std::chrono::nanoseconds &period);
  std::chrono::nanoseconds RemainingTime(TimerHandle handle);
  void CancelTimer(TimerHandle handle);

 private:
  Machine &machine_;
  Arena arena_;
  IntrusiveList<Timer> timers_;
  IntrusiveList<Timer> free_timers_;
  std::chrono::high_resolution_clock::time_point base_time_;
};
}
}

#endif
