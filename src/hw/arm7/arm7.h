#ifndef ARM7_H
#define ARM7_H

#include "hw/machine.h"
#include "hw/memory.h"

namespace re {
namespace hw {
class Dreamcast;

namespace arm7 {

class ARM7 : public Device, public ExecuteInterface, public MemoryInterface {
 public:
  AM_DECLARE(data_map);

  ARM7(Dreamcast &dc);

  bool Init() final;

private:
  void Run(const std::chrono::nanoseconds &delta) final;

  Dreamcast &dc_;
};
}
}
}

#endif
