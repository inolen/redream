#ifndef EMULATOR_H
#define EMULATOR_H

#include "cpu/sh4.h"
#include "cpu/backend/backend.h"
#include "cpu/frontend/frontend.h"
#include "cpu/runtime.h"
#include "holly/holly.h"
#include "renderer/backend.h"
#include "system/system.h"

namespace dreavm {
namespace emu {

class Emulator {
 public:
  Emulator(system::System &sys);
  ~Emulator();

  bool Init();
  bool Launch(const char *path);
  void Tick();

 private:
  void InitMemory();
  bool LoadBios(const char *path);
  bool LoadFlash(const char *path);

  void Reset();
  bool LaunchBIN(const char *path);
  bool LaunchGDI(const char *path);
  void PumpEvents();
  void RenderFrame();

  system::System &sys_;
  emu::Scheduler scheduler_;
  emu::Memory memory_;
  cpu::Runtime runtime_;
  cpu::SH4 processor_;
  holly::Holly holly_;
  cpu::frontend::Frontend *rt_frontend_;
  cpu::backend::Backend *rt_backend_;
  renderer::Backend *rb_;
  uint8_t *bios_;
  uint8_t *flash_;
  uint8_t *ram_;
  uint8_t *unassigned_;
};
}
}

#endif
