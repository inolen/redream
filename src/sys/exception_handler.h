#ifndef EXCEPTION_HANDLER_H
#define EXCEPTION_HANDLER_H

#include <stdbool.h>
#include <stdint.h>

struct exception;
struct exception_handler;

typedef bool (*exception_handler_cb)(void *data, struct exception *ex);

enum exception_type {
  EX_ACCESS_VIOLATION,
  EX_INVALID_INSTRUCTION,
};

union thread_state {
  struct {
    uint64_t rax, rcx, rdx, rbx, rsp, rbp, rsi, rdi, r8, r9, r10, r11, r12, r13,
        r14, r15, rip;
  };
  uint64_t r[17];
};

struct exception {
  enum exception_type type;
  uintptr_t fault_addr;
  uintptr_t pc;
  union thread_state thread_state;
};

bool exception_handler_install();
bool exception_handler_install_platform();
void exception_handler_uninstall();
void exception_handler_uninstall_platform();
struct exception_handler *exception_handler_add(void *data,
                                                exception_handler_cb cb);
void exception_handler_remove(struct exception_handler *handler);
bool exception_handler_handle(struct exception *ex);

#endif
