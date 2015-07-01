#ifndef DEVICE_H
#define DEVICE_H

#include <stdint.h>

namespace dreavm {
namespace emu {

class Device {
 public:
  virtual ~Device(){};

  virtual int64_t GetClockFrequency() = 0;
  virtual int64_t Execute(int64_t cycles) = 0;
};
}
}

#endif
