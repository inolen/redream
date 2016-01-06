#include <cmath>
#include <chrono>
#include "hw/scheduler.h"

using namespace dvm::hw;

Scheduler::Scheduler() : base_time_() {}

DeviceHandle Scheduler::AddDevice(Device *device) {
  devices_.push_back(DeviceInfo(device));
  return static_cast<DeviceHandle>(devices_.size() - 1);
}

void Scheduler::Tick(const std::chrono::nanoseconds &delta) {
  auto next_time = base_time_ + delta;

  while (base_time_ < next_time) {
    std::chrono::high_resolution_clock::time_point target_time = next_time;

    for (auto &info : devices_) {
      auto delta = std::chrono::duration_cast<std::chrono::nanoseconds>(
          target_time - info.current_time);
      int cycles_per_second = info.device->GetClockFrequency();
      int cycles_to_run = static_cast<int>(
          (delta.count() * static_cast<int64_t>(cycles_per_second)) /
          NS_PER_SEC);
      int ran = info.device->Run(cycles_to_run);

      info.current_time += std::chrono::nanoseconds(
          (ran * static_cast<int64_t>(NS_PER_SEC)) / cycles_per_second);
    }

    base_time_ = target_time;
  }
}
