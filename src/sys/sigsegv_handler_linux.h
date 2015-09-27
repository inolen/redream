#ifndef SIGSEGV_HANDLER_LINUX
#define SIGSEGV_HANDLER_LINUX

#include <thread>
#include "sys/sigsegv_handler.h"

namespace dreavm {
namespace sys {

class SIGSEGVHandlerLinux : public SIGSEGVHandler {
 public:
  ~SIGSEGVHandlerLinux();

 protected:
  bool Init();

 private:
};
}
}

#endif
