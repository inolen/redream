#include "core/core.h"
#include "sys/memory.h"

namespace dreavm {
namespace sys {

static ExceptionHandlerHandle exc_handler;
static WatchTree watches;

static bool HandleException(void *ctx, Exception &ex);

static struct _memory_exception_setup {
  _memory_exception_setup() {
    exc_handler =
        ExceptionHandler::instance().AddHandler(nullptr, &HandleException);
  }
  ~_memory_exception_setup() {
    ExceptionHandler::instance().RemoveHandler(exc_handler);
  }
} memory_exception_setup;

static bool HandleException(void *ctx, Exception &ex) {
  auto range_it = watches.intersect(ex.fault_addr, ex.fault_addr);
  auto it = range_it.first;
  auto end = range_it.second;

  while (it != end) {
    WatchTree::node_type *node = *(it++);
    Watch &watch = node->value;

    // call the handler for this access watch
    watch.handler(watch.ctx, ex, watch.data);

    if (watch.type == WATCH_SINGLE_WRITE) {
      // restore page permissions
      CHECK(ProtectPages(watch.ptr, watch.size, ACC_READWRITE));

      watches.Remove(node);
    }
  }

  return range_it.first != range_it.second;
}

WatchHandle AddSingleWriteWatch(void *ptr, size_t size, WatchHandler handler,
                                void *ctx, void *data) {
  // page align the range to be watched
  size_t page_size = GetPageSize();
  ptr = reinterpret_cast<void *>(dreavm::align(
      reinterpret_cast<uintptr_t>(ptr), static_cast<uintptr_t>(page_size)));
  size = dreavm::align(size, page_size);

  // disable writing to the pages
  CHECK(ProtectPages(ptr, size, ACC_READONLY));

  uintptr_t start = reinterpret_cast<uintptr_t>(ptr);
  uintptr_t end = start + size - 1;
  WatchHandle handle = watches.Insert(
      start, end, Watch{WATCH_SINGLE_WRITE, handler, ctx, data, ptr, size});

  return handle;
}

void RemoveAccessWatch(WatchHandle handle) { watches.Remove(handle); }
}
}
