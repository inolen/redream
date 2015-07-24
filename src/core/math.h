#ifndef DREAVM_MATH_H
#define DREAVM_MATH_H

namespace dreavm {
namespace core {

template <typename T>
T align(T v, T alignment) {
  return (v + alignment - 1) & -alignment;
}
}
}

#endif
