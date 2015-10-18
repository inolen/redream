#ifndef SEGFAULT_HANDLER_WIN
#define SEGFAULT_HANDLER_WIN

#include <thread>
#include "sys/segfault_handler.h"

namespace dreavm {
namespace sys {

class SegfaultHandlerWin : public SegfaultHandler {
 public:
  ~SegfaultHandlerWin();

  bool Init();
};
}
}

#endif
