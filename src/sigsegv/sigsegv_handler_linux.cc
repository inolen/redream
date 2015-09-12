#include <sys/mman.h>
#include <signal.h>
#include <unistd.h>
#include "core/core.h"
#include "sigsegv/sigsegv_handler_linux.h"

using namespace dreavm::sigsegv;

SIGSEGVHandler *dreavm::sigsegv::CreateSIGSEGVHandler() {
  return new SIGSEGVHandlerLinux();
}

static struct sigaction old_sa;

static void SignalHandler(int signo, siginfo_t *info, void *ctx) {
  ucontext_t *uctx = reinterpret_cast<ucontext_t *>(ctx);

  uintptr_t rip = uctx->uc_mcontext.gregs[REG_RIP];
  uintptr_t fault_addr = reinterpret_cast<uintptr_t>(info->si_addr);
  bool handled =
      SIGSEGVHandler::global_handler()->HandleAccessFault(rip, fault_addr);

  if (!handled) {
    // call into the original handler if the installed handler fails to handle
    // the signal
    (*old_sa.sa_sigaction)(signo, info, ctx);
  }
}

SIGSEGVHandlerLinux::~SIGSEGVHandlerLinux() {
  sigaction(SIGSEGV, &old_sa, nullptr);
}

bool SIGSEGVHandlerLinux::Init() {
  struct sigaction new_sa;
  new_sa.sa_flags = SA_SIGINFO;
  sigemptyset(&new_sa.sa_mask);
  new_sa.sa_sigaction = &SignalHandler;

  if (sigaction(SIGSEGV, &new_sa, &old_sa) != 0) {
    return false;
  }

  return true;
}

int SIGSEGVHandlerLinux::GetPageSize() { return getpagesize(); }

bool SIGSEGVHandlerLinux::Protect(void *ptr, int size, PageAccess access) {
  int prot = PROT_NONE;
  switch (access) {
    case ACC_READONLY:
      prot = PROT_READ;
      break;
    case ACC_READWRITE:
      prot = PROT_READ | PROT_WRITE;
      break;
  }

  return mprotect(ptr, size, prot) == 0;
}
