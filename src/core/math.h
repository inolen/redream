#ifndef DREAVM_MATH_H
#define DREAVM_MATH_H

#include <functional>

namespace dvm {

template <typename T>
T align(T v, T alignment) {
  return (v + alignment - 1) & ~(alignment - 1);
}

#if PLATFORM_LINUX || PLATFORM_DARWIN
inline int clz(uint32_t v) { return __builtin_clz(v); }
inline int clz(uint64_t v) { return __builtin_clzll(v); }
inline int ctz(uint32_t v) { return __builtin_ctz(v); }
inline int ctz(uint64_t v) { return __builtin_ctzll(v); }
#else
inline int clz(uint32_t v) {
  unsigned long r = 0;
  _BitScanReverse(&r, v);
  return 31 - r;
}
inline int clz(uint64_t v) {
  unsigned long r = 0;
  _BitScanReverse64(&r, v);
  return 63 - r;
}
inline int ctz(uint32_t v) {
  unsigned long r = 0;
  _BitScanForward(&r, v);
  return r;
}
inline int ctz(uint64_t v) {
  unsigned long r = 0;
  _BitScanForward64(&r, v);
  return r;
}
#endif
}

#endif
