#include <stdlib.h>
#include "sys/memory.h"
#include "core/assert.h"
#include "core/interval_tree.h"
#include "core/list.h"
#include "core/math.h"
#include "sys/exception_handler.h"

#define MAX_WATCHES 1024

struct memory_watch {
  enum memory_watch_type type;
  memory_watch_cb cb;
  void *data;
  struct interval_node tree_it;
  struct list_node list_it;
};

struct memory_watcher {
  struct exception_handler *exc_handler;
  struct rb_tree tree;
  struct memory_watch watches[MAX_WATCHES];
  struct list free_watches;
  struct list live_watches;
};

static struct memory_watcher *s_watcher;

static bool watcher_handle_exception(void *ctx, struct exception *ex);

static void watcher_create() {
  s_watcher = calloc(1, sizeof(struct memory_watcher));

  s_watcher->exc_handler =
      exception_handler_add(NULL, &watcher_handle_exception);

  for (int i = 0; i < MAX_WATCHES; i++) {
    struct memory_watch *watch = &s_watcher->watches[i];
    list_add(&s_watcher->free_watches, &watch->list_it);
  }
}

static void watcher_destroy() {
  exception_handler_remove(s_watcher->exc_handler);

  free(s_watcher);

  s_watcher = NULL;
}

static bool watcher_handle_exception(void *ctx, struct exception *ex) {
  bool handled = false;

  struct interval_tree_it it;
  struct interval_node *n = interval_tree_iter_first(
      &s_watcher->tree, ex->fault_addr, ex->fault_addr, &it);

  while (n) {
    handled = true;

    struct interval_node *next = interval_tree_iter_next(&it);
    struct memory_watch *watch = container_of(n, struct memory_watch, tree_it);

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

struct memory_watch *add_single_write_watch(const void *ptr, size_t size,
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
  struct memory_watch *watch =
      list_first_entry(&s_watcher->free_watches, struct memory_watch, list_it);
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

void remove_memory_watch(struct memory_watch *watch) {
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
