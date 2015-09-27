#include "core/core.h"
#include "core/interval_tree.h"
#include "emu/profiler.h"
#include "jit/runtime.h"
#include "sys/memory.h"
#include "sys/sigsegv_handler.h"

using namespace dreavm::sys;

SIGSEGVHandler *dreavm::sys::SIGSEGVHandler::instance_ = nullptr;

SIGSEGVHandler *SIGSEGVHandler::instance() {
  if (instance_) {
    return instance_;
  }

  instance_ = CreateSIGSEGVHandler();

  if (!instance_->Init()) {
    LOG_WARNING("Failed to initialize SIGSEGV handler");

    delete instance_;
    instance_ = nullptr;
  }

  return instance_;
}

SIGSEGVHandler::~SIGSEGVHandler() { instance_ = nullptr; }

void SIGSEGVHandler::AddWatch(void *ptr, size_t size, WatchHandler handler,
                              void *ctx, void *data) {
  // page align the range to be watched
  size_t page_size = GetPageSize();
  ptr = reinterpret_cast<void *>(dreavm::align(
      reinterpret_cast<uintptr_t>(ptr), static_cast<uintptr_t>(page_size)));
  size = dreavm::align(size, page_size);

  uintptr_t start = reinterpret_cast<uintptr_t>(ptr);
  uintptr_t end = start + size - 1;
  watches_.Insert(start, end,
                  Watch(WATCH_DEFAULT, handler, ctx, data, ptr, size));

  UpdateStats();
}

void SIGSEGVHandler::AddSingleWriteWatch(void *ptr, size_t size,
                                         WatchHandler handler, void *ctx,
                                         void *data) {
  // page align the range to be watched
  size_t page_size = GetPageSize();
  ptr = reinterpret_cast<void *>(dreavm::align(
      reinterpret_cast<uintptr_t>(ptr), static_cast<uintptr_t>(page_size)));
  size = dreavm::align(size, page_size);

  // write protect the pages
  CHECK(ProtectPages(ptr, size, ACC_READONLY));

  uintptr_t start = reinterpret_cast<uintptr_t>(ptr);
  uintptr_t end = start + size - 1;
  watches_.Insert(start, end,
                  Watch(WATCH_SINGLE_WRITE, handler, ctx, data, ptr, size));

  UpdateStats();
}

bool SIGSEGVHandler::HandleAccessFault(uintptr_t rip, uintptr_t fault_addr) {
  auto range_it = watches_.intersect(fault_addr, fault_addr);
  auto it = range_it.first;
  auto end = range_it.second;

  while (it != end) {
    WatchTree::node_type *node = *(it++);
    Watch &watch = node->value;

    watch.handler(watch.ctx, watch.data, rip, fault_addr);

    // remove single instance watches
    if (watch.type == WATCH_SINGLE_WRITE) {
      CHECK(ProtectPages(watch.ptr, watch.size, ACC_READWRITE));
      watches_.Remove(node);
    }
  }

  UpdateStats();

  return range_it.first != range_it.second;
}

void SIGSEGVHandler::UpdateStats() {
  PROFILER_COUNT("Watches", watches_.Size());
}
