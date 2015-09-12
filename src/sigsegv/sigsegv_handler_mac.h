#ifndef SIGSEGV_HANDLER_MAC
#define SIGSEGV_HANDLER_MAC

#include <thread>
#include "sigsegv/sigsegv_handler.h"

namespace dreavm {
namespace sigsegv {

class SIGSEGVHandlerMac : public SIGSEGVHandler {
 public:
  ~SIGSEGVHandlerMac();

 protected:
  bool Init();
  int GetPageSize();
  bool Protect(void *ptr, int size, PageAccess access);

 private:
  void ThreadEntry();

  mach_port_t listen_port_;
  std::thread thread_;
};
}
}

#endif
