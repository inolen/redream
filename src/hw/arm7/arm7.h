#ifndef ARM7_H
#define ARM7_H

#include "hw/machine.h"

namespace re {
namespace hw {
class Dreamcast;

namespace arm7 {

class ARM7 : public Device, public ExecuteInterface {
 public:
  ARM7(Dreamcast &dc);

  bool Init() final;

 private:
  // ExecuteInterface
  void Run(const std::chrono::nanoseconds &delta) final;

  Dreamcast &dc_;
  Memory *memory_;
};
}
}
}

#endif
