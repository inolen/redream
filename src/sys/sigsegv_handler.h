#ifndef SIGSEGV_HANDLER_H
#define SIGSEGV_HANDLER_H

#include <functional>
#include <memory>
#include "core/interval_tree.h"

namespace dreavm {
namespace sys {

// implemented in the platform specific souce file
class SIGSEGVHandler;
extern SIGSEGVHandler *CreateSIGSEGVHandler();

enum PageAccess { ACC_READONLY, ACC_READWRITE };

typedef std::function<void(void *, void *)> WriteWatchHandler;

struct WriteWatch {
  WriteWatch(WriteWatchHandler handler, void *ctx, void *data,
             uintptr_t physical_start, uintptr_t physical_end)
      : handler(handler),
        ctx(ctx),
        data(data),
        physical_start(physical_start),
        physical_end(physical_end) {}

  WriteWatchHandler handler;
  void *ctx;
  void *data;
  uintptr_t physical_start;
  uintptr_t physical_end;
};

typedef IntervalTree<WriteWatch> WatchTree;

class SIGSEGVHandler {
 public:
  static SIGSEGVHandler *global_handler() { return global_handler_; }

  static SIGSEGVHandler *Install();

  virtual ~SIGSEGVHandler();

  void AddWriteWatch(void *ptr, int size, WriteWatchHandler handler, void *ctx,
                     void *data);
  bool HandleAccessFault(uintptr_t rip, uintptr_t fault_addr);

 protected:
  static SIGSEGVHandler *global_handler_;

  virtual bool Init() = 0;
  virtual int GetPageSize() = 0;
  virtual bool Protect(void *ptr, int size, PageAccess access) = 0;

  WatchTree write_watches_;
};
}
}

#endif
