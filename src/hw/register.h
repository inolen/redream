#ifndef REGISTER_H
#define REGISTER_H

#include <stdint.h>
#include "core/delegate.h"

namespace re {
namespace hw {

static const uint8_t R = 0x1;
static const uint8_t W = 0x2;
static const uint8_t RW = 0x3;
static const uint8_t UNDEFINED = 0x0;

struct Register;

typedef delegate<uint32_t(Register &)> RegisterReadDelegate;
typedef delegate<void(Register &, uint32_t)> RegisterWriteDelegate;

struct Register {
  Register() : flags(RW), value(0) {}
  Register(uint8_t flags, uint32_t value) : flags(flags), value(value) {}
  Register(uint8_t flags, uint32_t value, RegisterReadDelegate read,
           RegisterWriteDelegate write)
      : flags(flags), value(value), read(read), write(write) {}

  uint8_t flags;
  uint32_t value;
  RegisterReadDelegate read;
  RegisterWriteDelegate write;
};
}
}

#endif
