#ifndef EXCEPTION_HANDLER_H
#define EXCEPTION_HANDLER_H

#include <stdint.h>

struct exception_state;
struct exception_handler;

typedef int (*exception_handler_cb)(void *, struct exception_state *);

enum exception_type {
  EX_ACCESS_VIOLATION,
  EX_INVALID_INSTRUCTION,
};

struct thread_state {
#if ARCH_A64
  union {
    struct {
      uint64_t r0, r1, r2, r3, r4, r5, r6, r7, r8, r9, r10, r11, r12, r13, r14,
          r15, r16, r17, r18, r19, r20, r21, r22, r23, r24, r25, r26, r27, r28,
          r29, r30;
    };
    uint64_t r[31];
  };
  uint64_t sp, pc, pstate;

  /* simd state */
  __uint128_t v[32];
  uint32_t fpsr, fpcr;
#elif ARCH_X64
  union {
    struct {
      uint64_t rax, rcx, rdx, rbx, rsp, rbp, rsi, rdi, r8, r9, r10, r11, r12,
          r13, r14, r15, rip;
    };
    uint64_t r[17];
  };
#endif
};

struct exception_state {
  enum exception_type type;
  uintptr_t fault_addr;
  uintptr_t pc;
  struct thread_state thread_state;
};

int exception_handler_install_platform();
void exception_handler_uninstall_platform();

struct exception_handler *exception_handler_add(void *data,
                                                exception_handler_cb cb);
void exception_handler_remove(struct exception_handler *handler);
int exception_handler_handle(struct exception_state *ex);

#endif
