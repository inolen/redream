#ifndef PROFILER_H
#define PROFILER_H

#include <microprofile.h>
#include "renderer/backend.h"
#include "system/keys.h"

#define PROFILER_SCOPE(group, name) \
  MICROPROFILE_SCOPEI(group, name, dreavm::emu::Profiler::ScopeColor(name))

#define PROFILER_GPU(name) \
  MICROPROFILE_SCOPEI("gpu", name, dreavm::emu::Profiler::ScopeColor(name))

#define PROFILER_RUNTIME(name) \
  MICROPROFILE_SCOPEI("runtime", name, dreavm::emu::Profiler::ScopeColor(name))

namespace dreavm {
namespace emu {

class Profiler {
 public:
  static uint32_t ScopeColor(const char *name);

  static void Init();

  static bool HandleInput(system::Keycode key, int16_t value);
  static bool HandleMouseMove(int x, int y);
  static void Render(renderer::Backend *backend);
};
}
}

#endif
