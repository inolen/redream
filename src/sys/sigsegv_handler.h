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

typedef std::function<bool(void *, void *, uintptr_t, uintptr_t)> WatchHandler;

enum WatchType { WATCH_DEFAULT, WATCH_SINGLE_WRITE };

struct Watch {
  Watch(WatchType type, WatchHandler handler, void *ctx, void *data, void *ptr,
        size_t size)
      : type(type),
        handler(handler),
        ctx(ctx),
        data(data),
        ptr(ptr),
        size(size) {}

  WatchType type;
  WatchHandler handler;
  void *ctx;
  void *data;
  void *ptr;
  size_t size;
};

typedef IntervalTree<Watch> WatchTree;

class SIGSEGVHandler {
 public:
  static SIGSEGVHandler *instance();

  virtual ~SIGSEGVHandler();

  void AddWatch(void *ptr, size_t size, WatchHandler handler, void *ctx,
                void *data);
  // void AddSingleReadWatch(void *ptr, size_t size, WatchHandler handler,
  //                         void *ctx, void *data);
  void AddSingleWriteWatch(void *ptr, size_t size, WatchHandler handler,
                           void *ctx, void *data);
  bool HandleAccessFault(uintptr_t rip, uintptr_t fault_addr);

 protected:
  static SIGSEGVHandler *instance_;

  virtual bool Init() = 0;
  void UpdateStats();

  WatchTree watches_;
};
}
}

#endif
