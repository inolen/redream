#include <signal.h>
#include "core/core.h"
#include "sys/segfault_handler_linux.h"

using namespace dreavm::sys;

SegfaultHandler *dreavm::sys::CreateSegfaultHandler() {
  return new SegfaultHandlerLinux();
}

static struct sigaction old_sa;

static void SignalHandler(int signo, siginfo_t *info, void *ctx) {
  ucontext_t *uctx = reinterpret_cast<ucontext_t *>(ctx);

  uintptr_t rip = uctx->uc_mcontext.gregs[REG_RIP];
  uintptr_t fault_addr = reinterpret_cast<uintptr_t>(info->si_addr);
  bool handled =
      SegfaultHandler::instance()->HandleAccessFault(rip, fault_addr);

  if (!handled) {
    // call into the original handler if the installed handler fails to handle
    // the signal
    (*old_sa.sa_sigaction)(signo, info, ctx);
  }
}

SegfaultHandlerLinux::~SegfaultHandlerLinux() {
  sigaction(SIGSEGV, &old_sa, nullptr);
}

bool SegfaultHandlerLinux::Init() {
  struct sigaction new_sa;
  new_sa.sa_flags = SA_SIGINFO;
  sigemptyset(&new_sa.sa_mask);
  new_sa.sa_sigaction = &SignalHandler;

  if (sigaction(SIGSEGV, &new_sa, &old_sa) != 0) {
    return false;
  }

  return true;
}
