#include "core/assert.h"
#include "core/list.h"

void list_add(list_t *list, list_node_t *n) {
  list_add_after(list, list->tail, n);
}

void list_add_after(list_t *list, list_node_t *after, list_node_t *n) {
  if (!after) {
    if (list->head) {
      n->next = list->head;
      n->next->prev = n;
    }

    list->head = n;
  } else {
    list_node_t *next = after->next;

    n->prev = after;
    n->prev->next = n;

    if (next) {
      n->next = next;
      n->next->prev = n;
    } else {
      n->next = NULL;
    }
  }

  // no need to check list->tail == NULL, in that case after would be NULL
  if (list->tail == after) {
    list->tail = n;
  }
}

void list_remove(list_t *list, list_node_t *n) {
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

void list_clear(list_t *list) {
  list->head = list->tail = NULL;
}

int list_empty(list_t *list) {
  return !list->head;
}
