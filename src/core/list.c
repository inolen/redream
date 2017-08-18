#include "core/list.h"

int list_empty(const struct list *list) {
  return !list->head;
}

void list_add(struct list *list, struct list_node *n) {
  list_add_after(list, list->tail, n);
}

void list_add_after(struct list *list, struct list_node *after,
                    struct list_node *n) {
  struct list_node *before = NULL;

  if (after) {
    before = after->next;
    n->prev = after;
    n->prev->next = n;
  } else {
    before = list->head;
    list->head = n;
    list->head->prev = NULL;
  }

  if (before) {
    n->next = before;
    n->next->prev = n;
  } else {
    list->tail = n;
    list->tail->next = NULL;
  }
}

void list_remove(struct list *list, struct list_node *n) {
  if (n->prev) {
    n->prev->next = n->next;
  } else {
    list->head = n->next;
  }

  if (n->next) {
    n->next->prev = n->prev;
  } else {
    list->tail = n->prev;
  }

  n->prev = n->next = NULL;
}

void list_clear(struct list *list) {
  list->head = list->tail = NULL;
}
