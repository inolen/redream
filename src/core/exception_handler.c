#include "core/exception_handler.h"
#include "core/core.h"
#include "core/list.h"

#define MAX_EXCEPTION_HANDLERS 32

struct exception_handler {
  void *data;
  exception_handler_cb cb;
  struct list_node it;
};

static struct exception_handler handlers[MAX_EXCEPTION_HANDLERS];
static struct list live_handlers;
static struct list free_handlers;

static void exception_handler_install() {
  for (int i = 0; i < MAX_EXCEPTION_HANDLERS; i++) {
    struct exception_handler *handler = &handlers[i];
    list_add(&free_handlers, &handler->it);
  }

  int res = exception_handler_install_platform();
  CHECK(res);
}

static void exception_handler_uninstall() {
  exception_handler_uninstall_platform();
}

struct exception_handler *exception_handler_add(void *data,
                                                exception_handler_cb cb) {
  if (list_empty(&live_handlers)) {
    exception_handler_install();
  }

  /* remove from free list */
  struct exception_handler *handler =
      list_first_entry(&free_handlers, struct exception_handler, it);
  CHECK_NOTNULL(handler);
  list_remove(&free_handlers, &handler->it);

  /* add to live list */
  handler->data = data;
  handler->cb = cb;
  list_add(&live_handlers, &handler->it);

  return handler;
}

void exception_handler_remove(struct exception_handler *handler) {
  list_remove(&live_handlers, &handler->it);
  list_add(&free_handlers, &handler->it);

  if (list_empty(&live_handlers)) {
    exception_handler_uninstall();
  }
}

int exception_handler_handle(struct exception_state *ex) {
  list_for_each_entry(handler, &live_handlers, struct exception_handler, it) {
    if (handler->cb(handler->data, ex)) {
      return 1;
    }
  }

  return 0;
}
