#ifndef EXCEPTION_HANDLER_H
#define EXCEPTION_HANDLER_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct re_exception_s;

typedef bool (*exception_handler_cb)(void *data, struct re_exception_s *ex);

typedef enum {
  EX_ACCESS_VIOLATION,
  EX_INVALID_INSTRUCTION,
} re_exception_type_t;

typedef union {
  struct {
    uint64_t rax, rcx, rdx, rbx, rsp, rbp, rsi, rdi, r8, r9, r10, r11, r12, r13,
        r14, r15, rip;
  };
  uint64_t r[17];
} re_thread_state_t;

typedef struct re_exception_s {
  re_exception_type_t type;
  uintptr_t fault_addr;
  uintptr_t pc;
  re_thread_state_t thread_state;
} re_exception_t;

bool exception_handler_install();
bool exception_handler_install_platform();
void exception_handler_uninstall();
void exception_handler_uninstall_platform();
struct exc_handler_s *exception_handler_add(void *data,
                                            exception_handler_cb cb);
void exception_handler_remove(struct exc_handler_s *handler);
bool exception_handler_handle(re_exception_t *ex);

#ifdef __cplusplus
}
#endif

#endif
