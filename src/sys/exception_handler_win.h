#ifndef EXCEPTION_HANDLER_WIN
#define EXCEPTION_HANDLER_WIN

#include <thread>
#include "sys/exception_handler.h"

namespace dreavm {
namespace sys {

class ExceptionHandlerWin : public ExceptionHandler {
 public:
  ~ExceptionHandlerWin();

  bool Init();
};
}
}

#endif
