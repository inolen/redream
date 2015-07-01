#ifndef EMULATOR_H
#define EMULATOR_H

#include "cpu/sh4.h"
#include "cpu/backend/interpreter/interpreter_backend.h"
#include "cpu/frontend/sh4/sh4_frontend.h"
#include "holly/holly.h"
#include "holly/maple_controller.h"
#include "renderer/backend.h"
#include "system/system.h"

namespace dreavm {
namespace emu {

class Emulator {
 public:
  Emulator(system::System &sys);
  ~Emulator();

  bool Init();

  bool LaunchBIN(const char *path);
  bool LaunchGDI(const char *path);
  void Tick();

 private:
  bool MountRam();
  bool LoadBios(const char *path);
  bool LoadFlash(const char *path);
  void PumpEvents();
  void RenderFrame();

  system::System &sys_;
  emu::Scheduler *scheduler_;
  emu::Memory *memory_;
  cpu::frontend::sh4::SH4Frontend *sh4_frontend_;
  cpu::backend::interpreter::InterpreterBackend *int_backend_;
  cpu::Runtime *runtime_;
  cpu::SH4 *processor_;
  holly::Holly *holly_;
  renderer::Backend *rb_;
};
}
}

#endif
