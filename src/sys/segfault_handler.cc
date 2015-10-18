#include "core/core.h"
#include "core/interval_tree.h"
#include "emu/profiler.h"
#include "jit/runtime.h"
#include "sys/memory.h"
#include "sys/segfault_handler.h"

using namespace dreavm::sys;

WatchHandle SegfaultHandler::AddAccessFaultWatch(void *ptr, size_t size,
                                                 WatchHandler handler,
                                                 void *ctx, void *data) {
  // page align the range to be watched
  size_t page_size = GetPageSize();
  ptr = reinterpret_cast<void *>(dreavm::align(
      reinterpret_cast<uintptr_t>(ptr), static_cast<uintptr_t>(page_size)));
  size = dreavm::align(size, page_size);

  uintptr_t start = reinterpret_cast<uintptr_t>(ptr);
  uintptr_t end = start + size - 1;
  WatchHandle handle = watches_.Insert(
      start, end, Watch(WATCH_ACCESS_FAULT, handler, ctx, data, ptr, size));

  UpdateStats();

  return handle;
}

WatchHandle SegfaultHandler::AddSingleWriteWatch(void *ptr, size_t size,
                                                 WatchHandler handler,
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
  WatchHandle handle = watches_.Insert(
      start, end, Watch(WATCH_SINGLE_WRITE, handler, ctx, data, ptr, size));

  UpdateStats();

  return handle;
}

void SegfaultHandler::RemoveWatch(WatchHandle handle) {
  watches_.Remove(handle);
}

bool SegfaultHandler::HandleAccessFault(uintptr_t rip, uintptr_t fault_addr) {
  auto range_it = watches_.intersect(fault_addr, fault_addr);
  auto it = range_it.first;
  auto end = range_it.second;

  while (it != end) {
    WatchTree::node_type *node = *(it++);
    Watch &watch = node->value;

    watch.handler(watch.ctx, watch.data, rip, fault_addr);

    if (watch.type == WATCH_SINGLE_WRITE) {
      // restore page permissions
      CHECK(ProtectPages(watch.ptr, watch.size, ACC_READWRITE));

      watches_.Remove(node);
    }
  }

  UpdateStats();

  return range_it.first != range_it.second;
}

void SegfaultHandler::UpdateStats() {
  PROFILER_COUNT("Watches", watches_.Size());
}
