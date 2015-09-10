#ifndef DEVICE_H
#define DEVICE_H

#include <stdint.h>

namespace dreavm {
namespace hw {

class Device {
 public:
  virtual ~Device(){};

  virtual uint32_t GetClockFrequency() = 0;
  virtual uint32_t Execute(uint32_t cycles) = 0;
};
}
}

#endif
