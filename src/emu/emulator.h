#ifndef EMULATOR_H
#define EMULATOR_H

#include <atomic>
#include <mutex>
#include "hw/holly/tile_renderer.h"
#include "hw/dreamcast.h"
#include "renderer/backend.h"
#include "sys/window.h"

namespace dvm {
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
  void ToggleTracing();

  // ran in the main thread, the graphics thread processes the TA output and
  // renders it along with various stats and debug menus
  void GraphicsThread();
  void PumpGraphicsEvents();
  void RenderGraphics();

  // ran from a separate thread, the core thread actually runs the emulator,
  // ultimately producing output for the graphics thread
  void CoreThread();
  void QueueCoreEvent(const sys::WindowEvent &ev);
  bool PollCoreEvent(sys::WindowEvent *ev);
  void PumpCoreEvents();

  sys::Window window_;
  hw::Dreamcast dc_;
  renderer::Backend *rb_;
  trace::TraceWriter *trace_writer_;
  hw::holly::TileRenderer *tile_renderer_;

  // variables accessed by both the graphics and core thread
  RingBuffer<sys::WindowEvent> core_events_;
  std::mutex core_events_mutex_;
  std::atomic<uint32_t> speed_;
  std::atomic<bool> running_;
};
}
}

#endif
