#include <windows.h>
#include "core/core.h"
#include "system/sigsegv_handler_win.h"

using namespace dreavm::system;

SIGSEGVHandler *dreavm::system::CreateSIGSEGVHandler() {
  return new SIGSEGVHandlerWin();
}

static LONG CALLBACK ExceptionHandler(PEXCEPTION_POINTERS ex_info) {
  auto code = ex_info->ExceptionRecord->ExceptionCode;
  if (code != STATUS_ACCESS_VIOLATION) {
    return EXCEPTION_CONTINUE_SEARCH;
  }

  uintptr_t rip = ex_info->ContextRecord->Rip;
  uintptr_t fault_addr = ex_info->ExceptionRecord->ExceptionInformation[1];
  bool handled =
      SIGSEGVHandler::global_handler()->HandleAccessFault(rip, fault_addr);

  if (!handled) {
    return EXCEPTION_CONTINUE_SEARCH;
  }

  return EXCEPTION_CONTINUE_EXECUTION;
}

SIGSEGVHandlerWin::~SIGSEGVHandlerWin() {
  RemoveVectoredExceptionHandler(ExceptionHandler);
}

bool SIGSEGVHandlerWin::Init() {
  return AddVectoredExceptionHandler(1, ExceptionHandler) != nullptr;
}

int SIGSEGVHandlerWin::GetPageSize() {
  SYSTEM_INFO system_info;
  GetSystemInfo(&system_info);
  return system_info.dwPageSize;
}

bool SIGSEGVHandlerWin::Protect(void *ptr, int size, PageAccess access) {
  DWORD new_protect = PAGE_NOACCESS;
  DWORD old_protect;
  switch (access) {
    case ACC_READONLY:
      new_protect = PAGE_READONLY;
      break;
    case ACC_READWRITE:
      new_protect = PAGE_READWRITE;
      break;
  }

  return VirtualProtect(ptr, size, new_protect, &old_protect);
}
