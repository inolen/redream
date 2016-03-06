#ifndef EMULATOR_H
#define EMULATOR_H

#include "emu/profiler.h"
#include "hw/dreamcast.h"
#include "sys/window.h"

namespace re {

namespace hw {
namespace holly {
class TileRenderer;
}
}

namespace renderer {
class Backend;
}

namespace emu {

class Emulator {
 public:
  Emulator();
  ~Emulator();

  void Run(const char *path);

 private:
  bool CreateDreamcast();
  void DestroyDreamcast();

  bool LoadBios(const char *path);
  bool LoadFlash(const char *path);
  bool LaunchBIN(const char *path);
  bool LaunchGDI(const char *path);
  void ToggleTracing();
  void RenderFrame();
  void PumpEvents();

  sys::Window window_;
  Profiler profiler_;
  hw::Dreamcast dc_;
  renderer::Backend *rb_;
  hw::holly::TileRenderer *tile_renderer_;
  uint32_t speed_;
  bool running_;
};
}
}

#endif
