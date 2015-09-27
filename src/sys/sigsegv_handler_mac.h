#ifndef SIGSEGV_HANDLER_MAC
#define SIGSEGV_HANDLER_MAC

#include <thread>
#include "sys/sigsegv_handler.h"

namespace dreavm {
namespace sys {

class SIGSEGVHandlerMac : public SIGSEGVHandler {
 public:
  ~SIGSEGVHandlerMac();

 protected:
  bool Init();

 private:
  void ThreadEntry();

  mach_port_t listen_port_;
  std::thread thread_;
};
}
}

#endif
