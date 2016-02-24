#define MICROPROFILE_TEXT_WIDTH 6
#define MICROPROFILE_TEXT_HEIGHT 12
#define MICROPROFILE_WEBSERVER 0
#define MICROPROFILE_GPU_TIMERS 0
#define MICROPROFILE_ENABLED 1
#define MICROPROFILEUI_ENABLED 1
#define MICROPROFILE_IMPL 1
#define MICROPROFILEUI_IMPL 1
#define MICROPROFILE_PER_THREAD_BUFFER_SIZE (1024 * 1024 * 20)
#define MICROPROFILE_CONTEXT_SWITCH_TRACE 0
#include <microprofile.h>
#include <microprofileui.h>
#include <string>
#include "emu/profiler.h"

using namespace re::emu;
using namespace re::renderer;
using namespace re::sys;

static Backend *s_current_backend = nullptr;

static float HueToRGB(float p, float q, float t) {
  if (t < 0.0f) {
    t += 1.0f;
  }
  if (t > 1.0f) {
    t -= 1.0f;
  }
  if (t < 1.0f / 6.0f) {
    return p + (q - p) * 6.0f * t;
  }
  if (t < 1.0f / 2.0f) {
    return q;
  }
  if (t < 2.0f / 3.0f) {
    return p + (q - p) * (2.0f / 3.0f - t) * 6.0f;
  }
  return p;
}

static void HSLToRGB(float h, float s, float l, uint8_t *r, uint8_t *g,
                     uint8_t *b) {
  float fr, fg, fb;

  if (s == 0.0f) {
    fr = fg = fb = l;
  } else {
    float q = l < 0.5f ? l * (1.0f + s) : l + s - l * s;
    float p = 2.0f * l - q;
    fr = HueToRGB(p, q, h + 1.0f / 3.0f);
    fg = HueToRGB(p, q, h);
    fb = HueToRGB(p, q, h - 1.0f / 3.0f);
  }

  *r = static_cast<uint8_t>(fr * 255);
  *g = static_cast<uint8_t>(fg * 255);
  *b = static_cast<uint8_t>(fb * 255);
}

Profiler::ThreadScope::ThreadScope(const char *name) {
  MicroProfileOnThreadCreate(name);
}

Profiler::ThreadScope::~ThreadScope() { MicroProfileOnThreadExit(); }

uint32_t Profiler::ScopeColor(const char *name) {
  auto hash = std::hash<std::string>();
  size_t name_hash = hash(std::string(name));
  float h = (name_hash % 360) / 360.0f;
  float s = 0.7f;
  float l = 0.6f;
  uint8_t r, g, b;
  HSLToRGB(h, s, l, &r, &g, &b);
  return (r << 16) | (g << 8) | b;
}

Profiler::Profiler() {
  // register and enable gpu and runtime group by default
  uint16_t gpu_group = MicroProfileGetGroup("gpu", MicroProfileTokenTypeCpu);
  g_MicroProfile.nActiveGroupWanted |= 1ll << gpu_group;

  uint16_t runtime_group =
      MicroProfileGetGroup("runtime", MicroProfileTokenTypeCpu);
  g_MicroProfile.nActiveGroupWanted |= 1ll << runtime_group;

  // render time / average time bars by default
  g_MicroProfile.nBars |= MP_DRAW_TIMERS | MP_DRAW_AVERAGE | MP_DRAW_CALL_COUNT;
}

bool Profiler::HandleInput(Keycode key, int16_t value) {
  if (key == K_F1) {
    if (value) {
      MicroProfileToggleDisplayMode();
    }
    return true;
  }

  if (key == K_MOUSE1) {
    MicroProfileMouseButton(value, 0);
    return true;
  }

  if (key == K_MOUSE2) {
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
  s_current_backend = backend;
  MicroProfileFlip();
  MicroProfileDraw(s_current_backend->video_width(),
                   s_current_backend->video_height());
}

//
// microprofile implementation
//
void MicroProfileDrawText(int x, int y, uint32_t color, const char *text,
                          uint32_t len) {
  // microprofile provides 24-bit rgb values for text color
  color = 0xff000000 | color;
  s_current_backend->RenderText2D(x, y, 12.0f, color, text);
}

void MicroProfileDrawBox(int x0, int y0, int x1, int y1, uint32_t color,
                         MicroProfileBoxType type) {
  // microprofile provides 32-bit argb values for box color, forward straight
  // through
  s_current_backend->RenderBox2D(x0, y0, x1, y1, color, (BoxType)type);
}

void MicroProfileDrawLine2D(uint32_t num_vertices, float *vertices,
                            uint32_t color) {
  // microprofile provides 24-bit rgb values for line color
  color = 0xff000000 | color;
  s_current_backend->RenderLine2D(vertices, num_vertices, color);
}
