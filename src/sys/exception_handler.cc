#include "core/interval_tree.h"
#include "emu/profiler.h"
#include "sys/exception_handler.h"

using namespace dvm::sys;

ExceptionHandler::ExceptionHandler() : next_handle_(0) {}

ExceptionHandlerHandle ExceptionHandler::AddHandler(
    void *ctx, ExceptionHandlerCallback cb) {
  ExceptionHandlerHandle handle = next_handle_++;
  handlers_.push_back(ExceptionHandlerEntry{handle, ctx, cb});
  return handle;
}

void ExceptionHandler::RemoveHandler(ExceptionHandlerHandle handle) {
  for (auto it = handlers_.begin(), end = handlers_.end(); it != end; ++it) {
    if (it->handle == handle) {
      handlers_.erase(it);
      break;
    }
  }
}

bool ExceptionHandler::HandleException(Exception &ex) {
  for (auto handler : handlers_) {
    if (handler.cb(handler.ctx, ex)) {
      return true;
    }
  }

  return false;
}
