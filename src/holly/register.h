#ifndef REGISTER_H
#define REGISTER_H

namespace dreavm {
namespace holly {

enum { R = 0x1, W = 0x2, RW = 0x3, UNDEFINED = 0x0 };

struct Register {
  Register() : offset(-1), flags(RW), value(0) {}
  Register(uint32_t offset, uint8_t flags, uint32_t value)
      : offset(offset), flags(flags), value(value) {}

  uint32_t offset;
  uint8_t flags;
  uint32_t value;
};
}
}

#endif
