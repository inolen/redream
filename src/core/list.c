#include "core/assert.h"
#include "core/list.h"

int list_empty(list_t *list) {
  return !list->head;
}

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

// Implements the mergesort for linked lists as described at
// http://www.chiark.greenend.org.uk/~sgtatham/algorithms/listsort.html
void list_sort(list_t *list, list_node_cmp cmp) {
  list_node_t *head = list->head;
  list_node_t *tail = NULL;
  int k = 1;

  while (true) {
    int merges = 0;
    list_node_t *p = head;

    head = NULL;
    tail = NULL;

    while (p) {
      // track the number of lists merged this pass
      merges++;

      // step q forward k places, tracking the size of p
      int psize = 0;
      int qsize = k;
      list_node_t *q = p;
      while (psize < k && q) {
        psize++;
        q = q->next;
      }

      // merge the list starting at p of length psize with the list starting
      // at q of at most, length qsize
      while (psize || (qsize && q)) {
        list_node_t *next;

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
