#ifndef DEVICE_H
#define DEVICE_H

#include <stdint.h>

namespace dreavm {
namespace hw {

class Device {
 public:
  virtual ~Device(){};

  virtual int GetClockFrequency() = 0;
  virtual int Execute(int cycles) = 0;
};
}
}

#endif
