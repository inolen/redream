#include "core/assert.h"
#include "hw/scheduler.h"

using namespace re::hw;

namespace re {
namespace hw {
const std::chrono::nanoseconds SUSPENDED(INT64_MAX);
}
}

Scheduler::Scheduler()
    : base_time_(), timers_(), num_timers_(0), next_handle_(0) {}

void Scheduler::Tick(const std::chrono::nanoseconds &delta) {
  auto next_time = base_time_ + delta;

  // fire callbacks for all expired timers
  for (int i = 0; i < num_timers_; i++) {
    Timer &timer = timers_[i];

    if (timer.remaining == SUSPENDED) {
      continue;
    }

    timer.remaining -= delta;

    while (timer.remaining <= std::chrono::nanoseconds::zero()) {
      timer.callback(timer.period);
      timer.remaining += timer.period;
    }
  }

  base_time_ = next_time;
}

TimerHandle Scheduler::AllocTimer(TimerCallback callback) {
  CHECK_LT(num_timers_, MAX_TIMERS);

  Timer &timer = timers_[num_timers_++];
  timer.handle = next_handle_++;
  timer.callback = callback;
  timer.period = SUSPENDED;
  timer.remaining = SUSPENDED;

  return timer.handle;
}

void Scheduler::AdjustTimer(TimerHandle handle,
                            const std::chrono::nanoseconds &period) {
  Timer &timer = GetTimer(handle);
  timer.period = period;
  timer.remaining = period;
}

void Scheduler::FreeTimer(TimerHandle handle) {
  // swap timer to be removed with last timer to keep a contiguous array
  Timer &timer = GetTimer(handle);
  timer = timers_[--num_timers_];
}

std::chrono::nanoseconds Scheduler::RemainingTime(TimerHandle handle) {
  Timer &timer = GetTimer(handle);
  return timer.remaining;
}

Timer &Scheduler::GetTimer(TimerHandle handle) {
  Timer *timer = nullptr;
  for (int i = 0; i < num_timers_; i++) {
    Timer *t = &timers_[i];

    if (t->handle == handle) {
      timer = t;
      break;
    }
  }
  CHECK_NOTNULL(timer);
  return *timer;
}
