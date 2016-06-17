#include <windows.h>
#include "sys/exception_handler_win.h"

static void CopyStateTo(PCONTEXT src, union thread_state *dst) {
  dst->rax = src->Rax;
  dst->rcx = src->Rcx;
  dst->rdx = src->Rdx;
  dst->rbx = src->Rbx;
  dst->rsp = src->Rsp;
  dst->rbp = src->Rbp;
  dst->rsi = src->Rsi;
  dst->rdi = src->Rdi;
  dst->r8 = src->R8;
  dst->r9 = src->R9;
  dst->r10 = src->R10;
  dst->r11 = src->R11;
  dst->r12 = src->R12;
  dst->r13 = src->R13;
  dst->r14 = src->R14;
  dst->r15 = src->R15;
  dst->rip = src->Rip;
}

static void CopyStateFrom(union thread_state *src, PCONTEXT dst) {
  dst->Rax = src->rax;
  dst->Rcx = src->rcx;
  dst->Rdx = src->rdx;
  dst->Rbx = src->rbx;
  dst->Rsp = src->rsp;
  dst->Rbp = src->rbp;
  dst->Rsi = src->rsi;
  dst->Rdi = src->rdi;
  dst->R8 = src->r8;
  dst->R9 = src->r9;
  dst->R10 = src->r10;
  dst->R11 = src->r11;
  dst->R12 = src->r12;
  dst->R13 = src->r13;
  dst->R14 = src->r14;
  dst->R15 = src->r15;
  dst->Rip = src->rip;
}

static LONG CALLBACK WinExceptionHandler(PEXCEPTION_POINTERS ex_info) {
  auto code = ex_info->ExceptionRecord->ExceptionCode;
  if (code != STATUS_ACCESS_VIOLATION && code != STATUS_ILLEGAL_INSTRUCTION) {
    return EXCEPTION_CONTINUE_SEARCH;
  }

  // convert signal to internal exception
  struct exception ex;
  ex.type = code == STATUS_ACCESS_VIOLATION ? EX_ACCESS_VIOLATION
                                            : EX_INVALID_INSTRUCTION;
  ex.fault_addr = ex_info->ExceptionRecord->ExceptionInformation[1];
  ex.pc = ex_info->ContextRecord->Rip;
  CopyStateTo(ex_info->ContextRecord, &ex.thread_state);

  // call exception handler, letting it potentially update the thread state
  bool handled = exception_handler_handle(&ex);

  if (!handled) {
    return EXCEPTION_CONTINUE_SEARCH;
  }

  // copy internal thread state back to mach thread state and restore
  CopyStateFrom(&ex.thread_state, ex_info->ContextRecord);

  return EXCEPTION_CONTINUE_EXECUTION;
}

ExceptionHandler &ExceptionHandler::instance() {
  static ExceptionHandlerWin instance;
  return instance;
}

ExceptionHandlerWin::~ExceptionHandlerWin() {
  RemoveVectoredExceptionHandler(WinExceptionHandler);
}

bool ExceptionHandlerWin::Init() {
  return AddVectoredExceptionHandler(1, WinExceptionHandler) != nullptr;
}
