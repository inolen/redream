#include <windows.h>
#include "core/core.h"
#include "sys/segfault_handler_win.h"

using namespace dreavm::sys;

SegfaultHandler *dreavm::sys::CreateSegfaultHandler() {
  return new SegfaultHandlerWin();
}

static LONG CALLBACK ExceptionHandler(PEXCEPTION_POINTERS ex_info) {
  auto code = ex_info->ExceptionRecord->ExceptionCode;
  if (code != STATUS_ACCESS_VIOLATION) {
    return EXCEPTION_CONTINUE_SEARCH;
  }

  uintptr_t rip = ex_info->ContextRecord->Rip;
  uintptr_t fault_addr = ex_info->ExceptionRecord->ExceptionInformation[1];
  bool handled =
      SegfaultHandler::instance()->HandleAccessFault(rip, fault_addr);

  if (!handled) {
    return EXCEPTION_CONTINUE_SEARCH;
  }

  return EXCEPTION_CONTINUE_EXECUTION;
}

SegfaultHandlerWin::~SegfaultHandlerWin() {
  RemoveVectoredExceptionHandler(ExceptionHandler);
}

bool SegfaultHandlerWin::Init() {
  return AddVectoredExceptionHandler(1, ExceptionHandler) != nullptr;
}
