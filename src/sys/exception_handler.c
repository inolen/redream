#include "sys/exception_handler.h"
#include "core/assert.h"
#include "core/list.h"

#define MAX_EXCEPTION_HANDLERS 32

struct exception_handler {
  void *data;
  exception_handler_cb cb;
  struct list_node it;
};

static struct exception_handler s_handlers[MAX_EXCEPTION_HANDLERS];
static struct list s_live_handlers;
static struct list s_free_handlers;
static int s_num_handlers;

bool exception_handler_install() {
  for (int i = 0; i < MAX_EXCEPTION_HANDLERS; i++) {
    struct exception_handler *handler = &s_handlers[i];
    list_add(&s_free_handlers, &handler->it);
  }

  return exception_handler_install_platform();
}

void exception_handler_uninstall() {
  exception_handler_uninstall_platform();
}

struct exception_handler *exception_handler_add(void *data,
                                                exception_handler_cb cb) {
  // remove from free list
  struct exception_handler *handler =
      list_first_entry(&s_free_handlers, struct exception_handler, it);
  CHECK_NOTNULL(handler);
  list_remove(&s_free_handlers, &handler->it);

  // add to live list
  handler->data = data;
  handler->cb = cb;
  list_add(&s_live_handlers, &handler->it);

  return handler;
}

void exception_handler_remove(struct exception_handler *handler) {
  list_remove(&s_live_handlers, &handler->it);
  list_add(&s_free_handlers, &handler->it);
}

bool exception_handler_handle(struct exception *ex) {
  list_for_each_entry(handler, &s_live_handlers, struct exception_handler, it) {
    if (handler->cb(handler->data, ex)) {
      return true;
    }
  }

  return false;
}
