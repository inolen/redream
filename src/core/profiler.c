#include "core/profiler.h"

void prof_enter(const char *name) {}

void prof_leave() {}

void prof_count(const char *name, int count) {}

// #include <microprofile.h>

// #define PROFILER_SCOPE(group, name) \
//   MICROPROFILE_SCOPEI(group, name, re::emu::ScopeColor(name))

// #define PROFILER_GPU(name) \
//   MICROPROFILE_SCOPEI("gpu", name, re::emu::ScopeColor(name))

// #define PROFILER_RUNTIME(name) \
//   MICROPROFILE_SCOPEI("runtime", name, re::emu::ScopeColor(name))

// #define PROFILER_COUNT(name, count) MICROPROFILE_COUNTER_SET(name, count)

// static inline float HueToRGB(float p, float q, float t) {
//   if (t < 0.0f) {
//     t += 1.0f;
//   }
//   if (t > 1.0f) {
//     t -= 1.0f;
//   }
//   if (t < 1.0f / 6.0f) {
//     return p + (q - p) * 6.0f * t;
//   }
//   if (t < 1.0f / 2.0f) {
//     return q;
//   }
//   if (t < 2.0f / 3.0f) {
//     return p + (q - p) * (2.0f / 3.0f - t) * 6.0f;
//   }
//   return p;
// }

// static inline void HSLToRGB(float h, float s, float l, uint8_t *r, uint8_t
// *g,
//                             uint8_t *b) {
//   float fr, fg, fb;

//   if (s == 0.0f) {
//     fr = fg = fb = l;
//   } else {
//     float q = l < 0.5f ? l * (1.0f + s) : l + s - l * s;
//     float p = 2.0f * l - q;
//     fr = HueToRGB(p, q, h + 1.0f / 3.0f);
//     fg = HueToRGB(p, q, h);
//     fb = HueToRGB(p, q, h - 1.0f / 3.0f);
//   }

//   *r = static_cast<uint8_t>(fr * 255);
//   *g = static_cast<uint8_t>(fg * 255);
//   *b = static_cast<uint8_t>(fb * 255);
// }

// static inline uint32_t ScopeColor(const char *name) {
//   auto hash = std::hash<std::string>();
//   size_t name_hash = hash(std::string(name));
//   float h = (name_hash % 360) / 360.0f;
//   float s = 0.7f;
//   float l = 0.6f;
//   uint8_t r, g, b;
//   HSLToRGB(h, s, l, &r, &g, &b);
//   return (r << 16) | (g << 8) | b;
// }
