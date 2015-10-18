#ifndef SEGFAULT_HANDLER_MAC
#define SEGFAULT_HANDLER_MAC

#include <thread>
#include "sys/segfault_handler.h"

namespace dreavm {
namespace sys {

class SegfaultHandlerMac : public SegfaultHandler {
 public:
  ~SegfaultHandlerMac();

  bool Init();

 private:
  void ThreadEntry();

  mach_port_t listen_port_;
  std::thread thread_;
};
}
}

#endif
