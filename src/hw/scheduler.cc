#include "core/assert.h"
#include "hw/scheduler.h"

using namespace re::hw;

Scheduler::Scheduler()
    : base_time_(), timers_(), num_timers_(0), next_handle_(0) {}

void Scheduler::Tick(const std::chrono::nanoseconds &delta) {
  auto next_time = base_time_ + delta;

  // fire callbacks for all expired timers
  for (int i = 0; i < num_timers_; i++) {
    Timer &timer = timers_[i];

    while (timer.expire <= next_time) {
      timer.callback(timer.period);
      timer.expire += timer.period;
    }
  }

  base_time_ = next_time;
}

TimerHandle Scheduler::AddTimer(const std::chrono::nanoseconds &period,
                                TimerCallback callback) {
  CHECK_LT(num_timers_, MAX_TIMERS);

  Timer &timer = timers_[num_timers_++];
  timer.handle = next_handle_++;
  timer.period = period;
  timer.expire = base_time_ + period;
  timer.callback = callback;

  return timer.handle;
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

bool Scheduler::RemoveTimer(TimerHandle handle) {
  if (handle == INVALID_TIMER) {
    return false;
  }

  // swap timer to be removed with last timer to keep a contiguous array
  Timer &timer = GetTimer(handle);
  timer = timers_[--num_timers_];

  return true;
}
