#include "core/assert.h"
#include "core/minmax_heap.h"
#include "hw/machine.h"
#include "hw/scheduler.h"

using namespace re::hw;

Scheduler::Scheduler(Machine &machine)
    : machine_(machine), arena_(sizeof(Timer) * 128), timers_(), base_time_() {}

void Scheduler::Tick(const std::chrono::nanoseconds &delta) {
  auto target_time = base_time_ + delta;

  while (base_time_ < target_time) {
    if (machine_.suspended()) {
      break;
    }

    // run devices up to the next timer
    auto next_time = target_time;
    Timer *next_timer = timers_.head();

    if (next_timer && next_timer->expire < next_time) {
      next_time = next_timer->expire;
    }

    // go ahead and update base time before running devices and expiring timers
    // in case one of them schedules a new timer
    auto slice = next_time - base_time_;
    base_time_ += slice;

    for (auto device : machine_.devices()) {
      ExecuteInterface *execute = device->execute();

      if (!execute || execute->suspended()) {
        continue;
      }

      execute->Run(slice);
    }

    // execute expired timers
    while (next_timer) {
      if (next_timer->expire > base_time_) {
        break;
      }

      next_timer->delegate();

      // free the timer
      Timer *next_next_timer = next_timer->next();
      timers_.Remove(next_timer);
      free_timers_.Append(next_timer);
      next_timer = next_next_timer;
    }
  }
}

TimerHandle Scheduler::ScheduleTimer(TimerDelegate delegate,
                                     const std::chrono::nanoseconds &period) {
  // allocate a timer instance from the pool
  Timer *timer = nullptr;

  if (free_timers_.head()) {
    timer = free_timers_.head();
    free_timers_.Remove(timer);
  } else {
    timer = arena_.Alloc<Timer>();
    new (timer) Timer();
  }

  timer->expire = base_time_ + period;
  timer->delegate = delegate;

  // insert it into the sorted list
  Timer *after = nullptr;

  for (auto t : timers_) {
    if (t->expire > timer->expire) {
      break;
    }

    after = t;
  }

  timers_.Insert(after, timer);

  return static_cast<TimerHandle>(timer);
}

std::chrono::nanoseconds Scheduler::RemainingTime(TimerHandle handle) {
  Timer *timer = static_cast<Timer *>(handle);
  return timer->expire - base_time_;
}

void Scheduler::CancelTimer(TimerHandle handle) {
  Timer *timer = static_cast<Timer *>(handle);
  timers_.Remove(timer);
  free_timers_.Append(timer);
}
