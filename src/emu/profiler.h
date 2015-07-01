#ifndef PROFILER_H
#define PROFILER_H

#include <microprofile.h>
#include "renderer/backend.h"
#include "system/keys.h"

#define PROFILER_SCOPE(group, name) \
  MICROPROFILE_SCOPEI(group, name, dreavm::emu::Profiler::ScopeColor(name))

#define PROFILER_SCOPE_F(group)            \
  MICROPROFILE_SCOPEI(group, __FUNCTION__, \
                      dreavm::emu::Profiler::ScopeColor(__FUNCTION__))

namespace dreavm {
namespace emu {

class Profiler {
 public:
  static uint32_t ScopeColor(const char *name);

  static bool Init();
  static void Shutdown();
  static bool HandleInput(system::Keycode key, int16_t value);
  static bool HandleMouseMove(int x, int y);
  static void Render(renderer::Backend *backend);
};
}
}

#endif
