#ifndef EMULATOR_H
#define EMULATOR_H

#include "hw/dreamcast.h"
#include "renderer/backend.h"
#include "sys/system.h"

namespace dreavm {
namespace emu {

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

  sys::System sys_;
  hw::Dreamcast dc_;
  renderer::Backend *rb_;
  trace::TraceWriter *trace_writer_;
};
}
}

#endif
