#ifndef SEGFAULT_HANDLER_LINUX
#define SEGFAULT_HANDLER_LINUX

#include <thread>
#include "sys/segfault_handler.h"

namespace dreavm {
namespace sys {

class SegfaultHandlerLinux : public SegfaultHandler {
 public:
  ~SegfaultHandlerLinux();

  bool Init();
};
}
}

#endif
