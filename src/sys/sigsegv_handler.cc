#include "core/core.h"
#include "core/interval_tree.h"
#include "emu/profiler.h"
#include "sys/sigsegv_handler.h"

using namespace dreavm::sys;

SIGSEGVHandler *dreavm::sys::SIGSEGVHandler::global_handler_ = nullptr;

SIGSEGVHandler *SIGSEGVHandler::Install() {
  if (global_handler_) {
    return global_handler_;
  }

  global_handler_ = CreateSIGSEGVHandler();

  if (!global_handler_->Init()) {
    LOG_WARNING("Failed to install SIGSEGV handler");

    delete global_handler_;
    global_handler_ = nullptr;
  }

  return global_handler_;
}

SIGSEGVHandler::~SIGSEGVHandler() { global_handler_ = nullptr; }

void SIGSEGVHandler::AddWriteWatch(void *ptr, int size,
                                   WriteWatchHandler handler, void *ctx,
                                   void *data) {
  int page_size = GetPageSize();
  uintptr_t physical_start = dreavm::align(reinterpret_cast<uintptr_t>(ptr),
                                           static_cast<uintptr_t>(page_size));
  uintptr_t physical_end =
      dreavm::align(reinterpret_cast<uintptr_t>(ptr) + size,
                    static_cast<uintptr_t>(page_size));

  // write protect the pages
  CHECK(Protect(reinterpret_cast<void *>(physical_start),
                static_cast<int>(physical_end - physical_start), ACC_READONLY));

  write_watches_.Insert(
      physical_start, physical_end - 1,
      WriteWatch(handler, ctx, data, physical_start, physical_end));

  // track number of watches
  PROFILER_COUNT("WriteWatches", write_watches_.Size());
}

bool SIGSEGVHandler::HandleAccessFault(uintptr_t rip, uintptr_t fault_addr) {
  WatchTree::node_type *node = write_watches_.Find(fault_addr, fault_addr);

  bool handled = node != nullptr;

  while (node) {
    WriteWatch &watch = node->value;

    watch.handler(watch.ctx, watch.data);

    // remove write protection
    CHECK(Protect(reinterpret_cast<void *>(watch.physical_start),
                  static_cast<int>(watch.physical_end - watch.physical_start),
                  ACC_READWRITE));

    write_watches_.Remove(node);

    node = write_watches_.Find(fault_addr, fault_addr);
  }

  // track number of watches
  PROFILER_COUNT("WriteWatches", write_watches_.Size());

  return handled;
}
