#define MICROPROFILE_TEXT_WIDTH 6
#define MICROPROFILE_TEXT_HEIGHT 12
#define MICROPROFILE_WEBSERVER 0
#define MICROPROFILE_ENABLED 1
#define MICROPROFILEUI_ENABLED 1
#define MICROPROFILE_IMPL 1
#define MICROPROFILEUI_IMPL 1
#define MICROPROFILE_PER_THREAD_BUFFER_SIZE (1024 * 1024 * 20)
#include <microprofile.h>
#include <microprofileui.h>

#include <functional>
#include <string>
#include "emu/profiler.h"

using namespace dreavm::emu;
using namespace dreavm::renderer;
using namespace dreavm::system;

Backend *g_backend = nullptr;

uint32_t Profiler::ScopeColor(const char *name) {
  auto hash = std::hash<std::string>();
  return hash(std::string(name)) & 0xffffff;
}

bool Profiler::Init() {
  MicroProfileOnThreadCreate("main");

  // register and enable runtime group by default
  uint16_t runtime_group =
      MicroProfileGetGroup("runtime", MicroProfileTokenTypeCpu);
  g_MicroProfile.nActiveGroupWanted |= 1ll << runtime_group;

  // render time / average time bars by default
  g_MicroProfile.nBars |= MP_DRAW_TIMERS | MP_DRAW_AVERAGE;

  return true;
}

void Profiler::Shutdown() {}

bool Profiler::HandleInput(Keycode key, int16_t value) {
  if (key == K_F1 && value) {
    MicroProfileToggleDisplayMode();
    return true;
  } else if (key == K_MOUSE1) {
    MicroProfileMouseButton(value, 0);
    return true;
  } else if (key == K_MOUSE2) {
    MicroProfileMouseButton(0, value);
    return true;
  }
  return false;
}

bool Profiler::HandleMouseMove(int x, int y) {
  MicroProfileMousePosition(x, y, 0);
  return true;
}

void Profiler::Render(Backend *backend) {
  g_backend = backend;
  MicroProfileFlip();
  MicroProfileDraw(g_backend->video_width(), g_backend->video_height());
}

//
// microprofile implementation
//
void MicroProfileDrawText(int x, int y, uint32_t color, const char *text,
                          uint32_t len) {
  // microprofile provides 24-bit rgb values for text color
  color = 0xff000000 | color;
  g_backend->RenderText2D(x, y, 12, color, text);
}

void MicroProfileDrawBox(int x0, int y0, int x1, int y1, uint32_t color,
                         MicroProfileBoxType type) {
  // microprofile provides 32-bit argb values for box color, forward straight
  // through
  g_backend->RenderBox2D(x0, y0, x1, y1, color, (BoxType)type);
}

void MicroProfileDrawLine2D(uint32_t num_vertices, float *vertices,
                            uint32_t color) {
  // microprofile provides 24-bit rgb values for line color
  color = 0xff000000 | color;
  g_backend->RenderLine2D(vertices, num_vertices, color);
}

uint32_t MicroProfileGpuInsertTimeStamp() { return 0; }

uint64_t MicroProfileGpuGetTimeStamp(uint32_t key) { return 0; }

uint64_t MicroProfileTicksPerSecondGpu() { return 0; }
