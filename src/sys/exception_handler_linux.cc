#include <signal.h>
#include "sys/exception_handler_linux.h"

static struct sigaction old_sigsegv;
static struct sigaction old_sigill;

static void CopyStateTo(mcontext_t *src, re_thread_state_t *dst) {
  dst->rax = src->gregs[REG_RAX];
  dst->rcx = src->gregs[REG_RCX];
  dst->rdx = src->gregs[REG_RDX];
  dst->rbx = src->gregs[REG_RBX];
  dst->rsp = src->gregs[REG_RSP];
  dst->rbp = src->gregs[REG_RBP];
  dst->rsi = src->gregs[REG_RSI];
  dst->rdi = src->gregs[REG_RDI];
  dst->r8 = src->gregs[REG_R8];
  dst->r9 = src->gregs[REG_R9];
  dst->r10 = src->gregs[REG_R10];
  dst->r11 = src->gregs[REG_R11];
  dst->r12 = src->gregs[REG_R12];
  dst->r13 = src->gregs[REG_R13];
  dst->r14 = src->gregs[REG_R14];
  dst->r15 = src->gregs[REG_R15];
  dst->rip = src->gregs[REG_RIP];
}

static void CopyStateFrom(re_thread_state_t *src, mcontext_t *dst) {
  dst->gregs[REG_RAX] = src->rax;
  dst->gregs[REG_RCX] = src->rcx;
  dst->gregs[REG_RDX] = src->rdx;
  dst->gregs[REG_RBX] = src->rbx;
  dst->gregs[REG_RSP] = src->rsp;
  dst->gregs[REG_RBP] = src->rbp;
  dst->gregs[REG_RSI] = src->rsi;
  dst->gregs[REG_RDI] = src->rdi;
  dst->gregs[REG_R8] = src->r8;
  dst->gregs[REG_R9] = src->r9;
  dst->gregs[REG_R10] = src->r10;
  dst->gregs[REG_R11] = src->r11;
  dst->gregs[REG_R12] = src->r12;
  dst->gregs[REG_R13] = src->r13;
  dst->gregs[REG_R14] = src->r14;
  dst->gregs[REG_R15] = src->r15;
  dst->gregs[REG_RIP] = src->rip;
}

static void SignalHandler(int signo, siginfo_t *info, void *ctx) {
  ucontext_t *uctx = reinterpret_cast<ucontext_t *>(ctx);

  // convert signal to internal exception
  re_exception_t ex;
  ex.type = signo == SIGSEGV ? EX_ACCESS_VIOLATION : EX_INVALID_INSTRUCTION;
  ex.fault_addr = reinterpret_cast<uintptr_t>(info->si_addr);
  ex.pc = uctx->uc_mcontext.gregs[REG_RIP];
  CopyStateTo(&uctx->uc_mcontext, &ex.thread_state);

  // call exception handler, letting it potentially update the thread state
  bool handled = exception_handler_handle(&ex);

  if (!handled) {
    // call into the original handler if the installed handler fails to handle
    // the signal
    struct sigaction *sa = signo == SIGSEGV ? &old_sigsegv : &old_sigill;
    (*sa->sa_sigaction)(signo, info, ctx);
    return;
  }

  // copy internal thread state back to mach thread state and restore
  CopyStateFrom(&ex.thread_state, &uctx->uc_mcontext);
}

ExceptionHandler &ExceptionHandler::instance() {
  static ExceptionHandlerLinux instance;
  return instance;
}

ExceptionHandlerLinux::~ExceptionHandlerLinux() {
  sigaction(SIGSEGV, &old_sigsegv, nullptr);
  sigaction(SIGILL, &old_sigill, nullptr);
}

bool ExceptionHandlerLinux::Init() {
  struct sigaction new_sa;
  new_sa.sa_flags = SA_SIGINFO;
  sigemptyset(&new_sa.sa_mask);
  new_sa.sa_sigaction = &SignalHandler;

  if (sigaction(SIGSEGV, &new_sa, &old_sigsegv) != 0) {
    return false;
  }

  if (sigaction(SIGILL, &new_sa, &old_sigill) != 0) {
    return false;
  }

  return true;
}
