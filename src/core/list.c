#include "core/list.h"
#include "core/assert.h"

int list_empty(struct list *list) {
  return !list->head;
}

void list_add(struct list *list, struct list_node *n) {
  list_add_after(list, list->tail, n);
}

void list_add_after(struct list *list, struct list_node *after,
                    struct list_node *n) {
  if (!after) {
    if (list->head) {
      n->next = list->head;
      n->next->prev = n;
    }

    list->head = n;
  } else {
    struct list_node *next = after->next;

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

// Implements the mergesort for linked lists as described at
// http://www.chiark.greenend.org.uk/~sgtatham/algorithms/listsort.html
void list_sort(struct list *list, list_node_cmp cmp) {
  struct list_node *head = list->head;
  struct list_node *tail = NULL;
  int k = 1;

  while (true) {
    int merges = 0;
    struct list_node *p = head;

    head = NULL;
    tail = NULL;

    while (p) {
      // track the number of lists merged this pass
      merges++;

      // step q forward k places, tracking the size of p
      int psize = 0;
      int qsize = k;
      struct list_node *q = p;
      while (psize < k && q) {
        psize++;
        q = q->next;
      }

      // merge the list starting at p of length psize with the list starting
      // at q of at most, length qsize
      while (psize || (qsize && q)) {
        struct list_node *next;

        if (!psize) {
          next = q;
          q = q->next;
          qsize--;
        } else if (!qsize || !q) {
          next = p;
          p = p->next;
          psize--;
        } else if (cmp(q, p) < 0) {
          next = q;
          q = q->next;
          qsize--;
        } else {
          next = p;
          p = p->next;
          psize--;
        }

        // move merged node to tail
        if (!tail) {
          head = next;
        } else {
          tail->next = next;
        }
        next->prev = tail;
        tail = next;
      }

      p = q;
    }

    if (tail) {
      tail->next = NULL;
    }

    // if only 1 pair of lists was merged, this is the end
    if (merges <= 1) {
      break;
    }

    k *= 2;
  }

  // update internal head and tail with sorted head and tail
  list->head = head;
  list->tail = tail;
}
