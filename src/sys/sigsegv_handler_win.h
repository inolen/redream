#ifndef SIGSEGV_HANDLER_WIN
#define SIGSEGV_HANDLER_WIN

#include <thread>
#include "sys/sigsegv_handler.h"

namespace dreavm {
namespace sys {

class SIGSEGVHandlerWin : public SIGSEGVHandler {
 public:
  ~SIGSEGVHandlerWin();

 protected:
  bool Init();
  int GetPageSize();
  bool Protect(void *ptr, int size, PageAccess access);

 private:
};
}
}

#endif
