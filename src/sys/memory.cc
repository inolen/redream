#include "core/math.h"
#include "sys/memory.h"

namespace re {
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

    // call the delegate for this access watch
    watch.delegate(ex, watch.data);

    if (watch.type == WATCH_SINGLE_WRITE) {
      // restore page permissions
      uintptr_t aligned_begin = node->low;
      size_t aligned_size = (node->high - node->low) + 1;
      CHECK(ProtectPages(reinterpret_cast<void *>(aligned_begin), aligned_size,
                         ACC_READWRITE));

      watches.Remove(node);
    }
  }

  return range_it.first != range_it.second;
}

WatchHandle AddSingleWriteWatch(void *ptr, size_t size, WatchDelegate delegate,
                                void *data) {
  // page align the range to be watched
  size_t page_size = GetPageSize();
  uintptr_t aligned_begin =
      re::align_down(reinterpret_cast<uintptr_t>(ptr), page_size);
  uintptr_t aligned_end =
      re::align_up(reinterpret_cast<uintptr_t>(ptr) + size, page_size) - 1;
  size_t aligned_size = (aligned_end - aligned_begin) + 1;

  // disable writing to the pages
  CHECK(ProtectPages(reinterpret_cast<void *>(aligned_begin), aligned_size,
                     ACC_READONLY));

  WatchHandle handle = watches.Insert(
      aligned_begin, aligned_end, Watch{WATCH_SINGLE_WRITE, delegate, data});

  return handle;
}

void RemoveAccessWatch(WatchHandle handle) { watches.Remove(handle); }
}
}
