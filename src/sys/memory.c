#include <stdlib.h>
#include "core/assert.h"
#include "core/list.h"
#include "core/interval_tree.h"
#include "core/math.h"
#include "sys/exception_handler.h"
#include "sys/memory.h"

static const int MAX_WATCHES = 1024;

typedef struct memory_watch_s {
  memory_watch_type_t type;
  memory_watch_cb cb;
  void *data;
  interval_node_t tree_it;
  list_node_t list_it;
} memory_watch_t;

typedef struct {
  struct re_exception_handler_s *exc_handler;
  rb_tree_t tree;
  memory_watch_t watches[MAX_WATCHES];
  list_t free_watches;
  list_t live_watches;
} memory_watcher_t;

static memory_watcher_t *s_watcher;

static bool watcher_handle_exception(void *ctx, re_exception_t *ex);

static void watcher_create() {
  s_watcher = calloc(1, sizeof(memory_watcher_t));

  s_watcher->exc_handler =
      exception_handler_add(NULL, &watcher_handle_exception);

  for (int i = 0; i < MAX_WATCHES; i++) {
    memory_watch_t *watch = &s_watcher->watches[i];
    list_add(&s_watcher->free_watches, &watch->list_it);
  }
}

static void watcher_destroy() {
  exception_handler_remove(s_watcher->exc_handler);

  free(s_watcher);

  s_watcher = NULL;
}

static bool watcher_handle_exception(void *ctx, re_exception_t *ex) {
  bool handled = false;

  interval_tree_iter_t it;
  interval_node_t *n = interval_tree_iter_first(
      &s_watcher->tree, ex->fault_addr, ex->fault_addr, &it);

  while (n) {
    handled = true;

    interval_node_t *next = interval_tree_iter_next(&it);
    memory_watch_t *watch = container_of(n, memory_watch_t, tree_it);

    // call callback for this access watch
    watch->cb(ex, watch->data);

    if (watch->type == WATCH_SINGLE_WRITE) {
      // restore page permissions
      uintptr_t aligned_begin = n->low;
      size_t aligned_size = (n->high - n->low) + 1;
      CHECK(protect_pages((void *)aligned_begin, aligned_size, ACC_READWRITE));

      interval_tree_remove(&s_watcher->tree, n);
    }

    n = next;
  }

  if (!s_watcher->tree.root) {
    watcher_destroy();
  }

  return handled;
}

memory_watch_t *add_single_write_watch(void *ptr, size_t size,
                                       memory_watch_cb cb, void *data) {
  if (!s_watcher) {
    watcher_create();
  }

  // page align the range to be watched
  size_t page_size = get_page_size();
  uintptr_t aligned_begin = align_down((uintptr_t)ptr, page_size);
  uintptr_t aligned_end = align_up((uintptr_t)ptr + size, page_size) - 1;
  size_t aligned_size = (aligned_end - aligned_begin) + 1;

  // disable writing to the pages
  CHECK(protect_pages((void *)aligned_begin, aligned_size, ACC_READONLY));

  // allocate new access watch
  memory_watch_t *watch =
      list_first_entry(&s_watcher->free_watches, memory_watch_t, list_it);
  CHECK_NOTNULL(watch);
  watch->type = WATCH_SINGLE_WRITE;
  watch->cb = cb;
  watch->data = data;

  // remove from free list
  list_remove(&s_watcher->free_watches, &watch->list_it);

  // add to live list
  list_add(&s_watcher->live_watches, &watch->list_it);

  // add to interval tree
  watch->tree_it.low = aligned_begin;
  watch->tree_it.high = aligned_end;

  interval_tree_insert(&s_watcher->tree, &watch->tree_it);

  return watch;
}

void remove_memory_watch(memory_watch_t *watch) {
  // remove from interval tree
  interval_tree_remove(&s_watcher->tree, &watch->tree_it);

  // remove from live list
  list_remove(&s_watcher->live_watches, &watch->list_it);

  // add to free list
  list_add(&s_watcher->free_watches, &watch->list_it);

  if (!s_watcher->tree.root) {
    watcher_destroy();
  }
}
