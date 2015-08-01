#ifndef DREAVM_HASH_H
#define DREAVM_HASH_H

#include <stddef.h>
#include <functional>

namespace dreavm {
namespace core {
template <class T>
inline void hash_combine(size_t &seed, const T &v) {
  std::hash<T> hasher;
  seed ^= hasher(v) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
}
}
}

#endif
