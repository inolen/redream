#ifndef EMULATOR_H
#define EMULATOR_H

#include "hw/dreamcast.h"
#include "renderer/backend.h"
#include "sys/window.h"

namespace dvm {
namespace emu {

enum {
  // number of deltas to use for speed stats
  MAX_SCHEDULER_DELTAS = 1000
};

class Emulator {
 public:
  Emulator();
  ~Emulator();

  void Run(const char *path);

 private:
  bool LoadBios(const char *path);
  bool LoadFlash(const char *path);
  bool LaunchBIN(const char *path);
  bool LaunchGDI(const char *path);

  void PumpEvents();
  void ToggleTracing();
  void RenderFrame();

  sys::Window window_;
  hw::Dreamcast dc_;
  renderer::Backend *rb_;
  trace::TraceWriter *trace_writer_;
  std::chrono::nanoseconds deltas_[MAX_SCHEDULER_DELTAS];
  unsigned delta_seq_;
  bool running_;
};
}
}

#endif
