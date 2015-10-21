#ifndef EXCEPTION_HANDLER_MAC
#define EXCEPTION_HANDLER_MAC

#include <thread>
#include "sys/exception_handler.h"

namespace dreavm {
namespace sys {

class ExceptionHandlerMac : public ExceptionHandler {
 public:
  ~ExceptionHandlerMac();

  bool Init();

 private:
  void ThreadEntry();

  mach_port_t listen_port_;
  std::thread thread_;
};
}
}

#endif
