#ifndef SEGFAULT_HANDLER_H
#define SEGFAULT_HANDLER_H

#include <functional>
#include <memory>
#include "core/interval_tree.h"
#include "core/singleton.h"

namespace dreavm {
namespace sys {

enum WatchType { WATCH_ACCESS_FAULT, WATCH_SINGLE_WRITE };

typedef std::function<void(void *, void *, uintptr_t, uintptr_t)> WatchHandler;

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
typedef WatchTree::node_type *WatchHandle;

class SegfaultHandler {
 public:
  static SegfaultHandler &instance();

  virtual ~SegfaultHandler() {}

  virtual bool Init() = 0;

  WatchHandle AddAccessFaultWatch(void *ptr, size_t size, WatchHandler handler,
                                  void *ctx, void *data);
  WatchHandle AddSingleWriteWatch(void *ptr, size_t size, WatchHandler handler,
                                  void *ctx, void *data);
  void RemoveWatch(WatchHandle handle);

  bool HandleAccessFault(uintptr_t rip, uintptr_t fault_addr);

 protected:
  SegfaultHandler(){};
  SegfaultHandler(SegfaultHandler const &) = delete;
  void operator=(SegfaultHandler const &) = delete;

  void UpdateStats();

  WatchTree watches_;
};
}
}

#endif
