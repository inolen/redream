#include <cmath>
#include <chrono>
#include "emu/scheduler.h"

using namespace dreavm::emu;

Scheduler::Scheduler()
    : next_timer_handle_(0),
      timeslice_(HZ_TO_NANO(60)),
      base_time_(),
      next_time_(base_time_ + timeslice_),
      tick_deltas_(),
      tick_idx_(0),
      perf_(0.0f) {}

DeviceHandle Scheduler::AddDevice(Device *device) {
  devices_.push_back(DeviceInfo(device));
  return static_cast<DeviceHandle>(devices_.size() - 1);
}

TimerHandle Scheduler::AddTimer(std::chrono::nanoseconds period,
                                TimerCallback callback) {
  TimerHandle handle = next_timer_handle_++;

  timers_.insert(Timer(handle, period, base_time_ + period, callback));

  return handle;
}

void Scheduler::AdjustTimer(TimerHandle handle,
                            std::chrono::nanoseconds period) {
  std::set<Timer>::iterator it;

  for (it = timers_.begin(); it != timers_.end(); ++it) {
    if (it->handle == handle) {
      break;
    }
  }

  if (it == timers_.end()) {
    return;
  }

  Timer t = *it;

  // erase the current timer instance
  timers_.erase(it);

  // requeue the timer with a new expiration time
  t.period = period;
  t.expire = base_time_ + period;

  timers_.insert(t);
}

void Scheduler::RemoveTimer(TimerHandle handle) {
  std::set<Timer>::iterator it;

  for (it = timers_.begin(); it != timers_.end(); ++it) {
    if (it->handle == handle) {
      break;
    }
  }

  if (it == timers_.end()) {
    return;
  }

  timers_.erase(it);
}

void Scheduler::Tick() {
  UpdatePerf();

  while (base_time_ < next_time_) {
    std::chrono::high_resolution_clock::time_point target_time = next_time_;

    // run devices up until the next timer expiration
    if (timers_.size() && timers_.begin()->expire < next_time_) {
      target_time = timers_.begin()->expire;
    }

    for (auto &info : devices_) {
      auto delta = std::chrono::duration_cast<std::chrono::nanoseconds>(
          target_time - info.current_time);
      uint32_t cycles_per_second = info.device->GetClockFrequency();
      uint32_t cycles_to_run = static_cast<uint32_t>(
          (delta.count() * static_cast<uint64_t>(cycles_per_second)) /
          NS_PER_SEC);
      uint32_t ran = info.device->Execute(cycles_to_run);

      info.current_time += std::chrono::nanoseconds(
          (ran * static_cast<uint64_t>(NS_PER_SEC)) / cycles_per_second);
    }

    base_time_ = target_time;

    // run any timer that's expired
    while (timers_.size() && timers_.begin()->expire <= base_time_) {
      auto it = timers_.begin();
      Timer t = *it;

      // erase the current timer instance
      timers_.erase(it);

      // run timer callback
      t.callback();

      // requeue the timer with a new expiration time
      t.expire += t.period;

      timers_.insert(t);
    }
  }

  next_time_ = base_time_ + timeslice_;
}

void Scheduler::UpdatePerf() {
  auto now = std::chrono::high_resolution_clock::now();
  auto delta =
      std::chrono::duration_cast<std::chrono::nanoseconds>(now - last_tick_);
  tick_deltas_[tick_idx_] = delta.count();
  tick_idx_ = (tick_idx_ + 1) % NUM_TICK_DELTAS;
  last_tick_ = now;

  int64_t total = 0;
  for (int i = 0; i < NUM_TICK_DELTAS; i++) {
    total += tick_deltas_[i];
  }
  perf_ = ((timeslice_.count() * NUM_TICK_DELTAS) / (float)total) * 100.0f;
}
