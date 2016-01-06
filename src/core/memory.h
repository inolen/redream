#ifndef DREAVM_MEMORY_H
#define DREAVM_MEMORY_H

namespace dvm {

template <typename T>
T load(const void *ptr) { return *reinterpret_cast<const T *>(ptr); }

template <typename T>
void store(void *ptr, T v) { *reinterpret_cast<T *>(ptr) = v; }

}

#endif
