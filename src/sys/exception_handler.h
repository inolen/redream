#ifndef EXCEPTION_HANDLER_H
#define EXCEPTION_HANDLER_H

#include <vector>

namespace re {
namespace sys {

// exception handler information
typedef int ExceptionHandlerHandle;

struct Exception;
typedef bool (*ExceptionHandlerCallback)(void *ctx, Exception &ex);

struct ExceptionHandlerEntry {
  ExceptionHandlerHandle handle;
  void *ctx;
  ExceptionHandlerCallback cb;
};

// generic exception structure for all platforms
enum ExceptionType {
  EX_ACCESS_VIOLATION,
  EX_INVALID_INSTRUCTION,
};

union ThreadState {
  struct {
    uint64_t rax, rcx, rdx, rbx, rsp, rbp, rsi, rdi, r8, r9, r10, r11, r12, r13,
        r14, r15, rip;
  };
  uint64_t r[17];
};

struct Exception {
  ExceptionType type;
  uintptr_t fault_addr;
  uintptr_t pc;
  ThreadState thread_state;
};

class ExceptionHandler {
 public:
  static ExceptionHandler &instance();

  virtual ~ExceptionHandler() {}

  virtual bool Init() = 0;

  ExceptionHandlerHandle AddHandler(void *ctx, ExceptionHandlerCallback cb);
  void RemoveHandler(ExceptionHandlerHandle handle);
  bool HandleException(Exception &ex);

 protected:
  ExceptionHandler();
  ExceptionHandler(ExceptionHandler const &) = delete;
  void operator=(ExceptionHandler const &) = delete;

  ExceptionHandlerHandle next_handle_;
  std::vector<ExceptionHandlerEntry> handlers_;
};
}
}

#endif
