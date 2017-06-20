#include <stdlib.h>
#include "core/memory.h"
#include "core/assert.h"
#include "core/exception_handler.h"
#include "core/interval_tree.h"
#include "core/list.h"
#include "core/math.h"

#define MAX_WATCHES 8192

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

static struct memory_watcher *watcher;

static int watcher_handle_exception(void *ctx, struct exception_state *ex);

static void watcher_create() {
  watcher = calloc(1, sizeof(struct memory_watcher));

  watcher->exc_handler = exception_handler_add(NULL, &watcher_handle_exception);

  for (int i = 0; i < MAX_WATCHES; i++) {
    struct memory_watch *watch = &watcher->watches[i];
    list_add(&watcher->free_watches, &watch->list_it);
  }
}

static void watcher_destroy() {
  exception_handler_remove(watcher->exc_handler);

  free(watcher);

  watcher = NULL;
}

static int watcher_handle_exception(void *ctx, struct exception_state *ex) {
  int handled = 0;

  struct interval_tree_it it;
  struct interval_node *n = interval_tree_iter_first(
      &watcher->tree, ex->fault_addr, ex->fault_addr, &it);

  while (n) {
    handled = 1;

    struct interval_node *next = interval_tree_iter_next(&it);
    struct memory_watch *watch = container_of(n, struct memory_watch, tree_it);

    /* call callback for this access watch */
    watch->cb(ex, watch->data);

    if (watch->type == WATCH_SINGLE_WRITE) {
      /* restore page permissions */
      uintptr_t aligned_begin = n->low;
      size_t aligned_size = (n->high - n->low) + 1;
      CHECK(protect_pages((void *)aligned_begin, aligned_size, ACC_READWRITE));

      remove_memory_watch(watch);
    }

    n = next;
  }

  if (watcher && !watcher->tree.root) {
    watcher_destroy();
  }

  return handled;
}

void remove_memory_watch(struct memory_watch *watch) {
  /* remove from interval tree */
  interval_tree_remove(&watcher->tree, &watch->tree_it);

  /* remove from live list */
  list_remove(&watcher->live_watches, &watch->list_it);

  /* add to free list */
  list_add(&watcher->free_watches, &watch->list_it);

  if (!watcher->tree.root) {
    watcher_destroy();
  }
}

struct memory_watch *add_single_write_watch(const void *ptr, size_t size,
                                            memory_watch_cb cb, void *data) {
  if (!watcher) {
    watcher_create();
  }

  /* page align the range to be watched */
  size_t page_size = get_page_size();
  uintptr_t aligned_begin = align_down((uintptr_t)ptr, page_size);
  uintptr_t aligned_end = align_up((uintptr_t)ptr + size, page_size) - 1;
  size_t aligned_size = (aligned_end - aligned_begin) + 1;

  /* disable writing to the pages */
  CHECK(protect_pages((void *)aligned_begin, aligned_size, ACC_READONLY));

  /* allocate new access watch */
  struct memory_watch *watch =
      list_first_entry(&watcher->free_watches, struct memory_watch, list_it);
  CHECK_NOTNULL(watch);
  watch->type = WATCH_SINGLE_WRITE;
  watch->cb = cb;
  watch->data = data;

  /* remove from free list */
  list_remove(&watcher->free_watches, &watch->list_it);

  /* add to live list */
  list_add(&watcher->live_watches, &watch->list_it);

  /* add to interval tree */
  watch->tree_it.low = aligned_begin;
  watch->tree_it.high = aligned_end;

  interval_tree_insert(&watcher->tree, &watch->tree_it);

  return watch;
}
