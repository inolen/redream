#ifndef REDREAM_MATH_H
#define REDREAM_MATH_H

#include <stdint.h>

#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#define MAX(a, b) (((a) > (b)) ? (a) : (b))

#define align_up(v, alignment) ((v + alignment - 1) & ~(alignment - 1))
#define align_down(v, alignment) (v & ~(alignment - 1))

#ifdef __cplusplus
extern "C" {
#endif

#if PLATFORM_LINUX || PLATFORM_DARWIN
static inline int clz32(uint32_t v) {
  return __builtin_clz(v);
}
static inline int clz64(uint64_t v) {
  return __builtin_clzll(v);
}
static inline int ctz32(uint32_t v) {
  return __builtin_ctz(v);
}
static inline int ctz64(uint64_t v) {
  return __builtin_ctzll(v);
}
#else
static inline int clz32(uint32_t v) {
  unsigned long r = 0;
  _BitScanReverse(&r, v);
  return 31 - r;
}
static inline int clz64(uint64_t v) {
  unsigned long r = 0;
  _BitScanReverse64(&r, v);
  return 63 - r;
}
static inline int ctz32(uint32_t v) {
  unsigned long r = 0;
  _BitScanForward(&r, v);
  return r;
}
static inline int ctz64(uint64_t v) {
  unsigned long r = 0;
  _BitScanForward64(&r, v);
  return r;
}
#endif

#ifdef __cplusplus
}
#endif

#endif
