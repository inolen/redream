#ifndef SIGSEGV_HANDLER_LINUX
#define SIGSEGV_HANDLER_LINUX

#include <thread>
#include "sigsegv/sigsegv_handler.h"

namespace dreavm {
namespace sigsegv {

class SIGSEGVHandlerLinux : public SIGSEGVHandler {
 public:
  ~SIGSEGVHandlerLinux();

 protected:
  bool Init();
  int GetPageSize();
  bool Protect(void *ptr, int size, PageAccess access);

 private:
};
}
}

#endif
