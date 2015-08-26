#ifndef DREAVM_MATH_H
#define DREAVM_MATH_H

#include "core/platform.h"

namespace dreavm {
namespace core {

template <typename T>
T align(T v, T alignment) {
  return (v + alignment - 1) & ~(alignment - 1);
}

#if defined(PLATFORM_LINUX) || defined(PLATFORM_DARWIN)
inline int clz(uint32_t v) { return __builtin_clz(v); }
inline int clz(uint64_t v) { return __builtin_clzll(v); }
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
#endif
}
}

#endif
