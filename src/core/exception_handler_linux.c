#include <signal.h>
#include <stdlib.h>
#include "core/core.h"
#include "core/exception_handler.h"

static struct sigaction old_sigsegv;
static struct sigaction old_sigill;

static inline void copy_state_to(mcontext_t *src, struct thread_state *dst) {
#if ARCH_A64
  struct fpsimd_context *simd = (struct fpsimd_context *)&src->__reserved;
  CHECK_EQ(simd->head.magic, FPSIMD_MAGIC);
  CHECK_EQ(simd->head.size, sizeof(struct fpsimd_context));

  for (int i = 0; i < ARRAY_SIZE(dst->r); i++) {
    dst->r[i] = src->regs[i];
  }

  dst->sp = src->sp;
  dst->pc = src->pc;
  dst->pstate = src->pstate;

  for (int i = 0; i < ARRAY_SIZE(dst->v); i++) {
    dst->v[i] = simd->vregs[i];
  }

  dst->fpsr = simd->fpsr;
  dst->fpcr = simd->fpcr;
#elif ARCH_X64
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
#endif
}

static inline void copy_state_from(struct thread_state *src, mcontext_t *dst) {
#if ARCH_A64
  struct fpsimd_context *simd = (struct fpsimd_context *)&dst->__reserved;
  CHECK_EQ(simd->head.magic, FPSIMD_MAGIC);
  CHECK_EQ(simd->head.size, sizeof(struct fpsimd_context));

  for (int i = 0; i < ARRAY_SIZE(src->r); i++) {
    dst->regs[i] = src->r[i];
  }

  dst->sp = src->sp;
  dst->pc = src->pc;
  dst->pstate = src->pstate;

  for (int i = 0; i < ARRAY_SIZE(src->v); i++) {
    simd->vregs[i] = src->v[i];
  }

  simd->fpsr = src->fpsr;
  simd->fpcr = src->fpcr;
#elif ARCH_X64
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
#endif
}

static void signal_handler(int signo, siginfo_t *info, void *ctx) {
  ucontext_t *uctx = ctx;

  /* convert signal to internal exception */
  struct exception_state ex;
  ex.type = signo == SIGSEGV ? EX_ACCESS_VIOLATION : EX_INVALID_INSTRUCTION;
  ex.fault_addr = (uintptr_t)info->si_addr;
#if ARCH_A64
  ex.pc = uctx->uc_mcontext.pc;
#elif ARCH_X64
  ex.pc = uctx->uc_mcontext.gregs[REG_RIP];
#endif
  copy_state_to(&uctx->uc_mcontext, &ex.thread_state);

  /* call exception handler, letting it potentially update the thread state */
  int handled = exception_handler_handle(&ex);

  if (!handled) {
    /* uninstall the signal handler if we couldn't handle it, let the kernel do
       its job */
    struct sigaction *old_sa = signo == SIGSEGV ? &old_sigsegv : &old_sigill;
    sigaction(signo, old_sa, NULL);
    return;
  }

  /* copy internal thread state back to mach thread state and restore */
  copy_state_from(&ex.thread_state, &uctx->uc_mcontext);
}

int exception_handler_install_platform() {
  struct sigaction new_sa;
  new_sa.sa_flags = SA_SIGINFO;
  sigemptyset(&new_sa.sa_mask);
  new_sa.sa_sigaction = &signal_handler;

  if (sigaction(SIGSEGV, &new_sa, &old_sigsegv) != 0) {
    return 0;
  }

  if (sigaction(SIGILL, &new_sa, &old_sigill) != 0) {
    return 0;
  }

  return 1;
}

void exception_handler_uninstall_platform() {
  sigaction(SIGSEGV, &old_sigsegv, NULL);
  sigaction(SIGILL, &old_sigill, NULL);
}
