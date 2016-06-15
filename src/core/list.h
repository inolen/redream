#ifndef REDREAM_LIST_H
#define REDREAM_LIST_H

#include <stdlib.h>
#include "core/core.h"

#ifdef __cplusplus
#include <type_traits>

extern "C" {
#endif

typedef struct list_node_s {
  struct list_node_s *prev;
  struct list_node_s *next;
} list_node_t;

typedef struct list_s {
  list_node_t *head;
  list_node_t *tail;
} list_t;

typedef int (*list_node_cmp)(const list_node_t *a, const list_node_t *b);

int list_empty(list_t *list);
void list_add(list_t *list, list_node_t *n);
void list_add_after(list_t *list, list_node_t *after, list_node_t *n);
void list_remove(list_t *list, list_node_t *n);
void list_clear(list_t *list);
void list_sort(list_t *list, list_node_cmp cmp);

#define list_for_each(list, it)                                               \
  for (list_node_t *it = (list)->head, *it##_next = it ? it->next : NULL; it; \
       it = it##_next, it##_next = it ? it->next : NULL)

#define list_entry(n, type, member) container_of(n, type, member)

#define list_add_after_entry(list, after, member, n) \
  list_add_after(list, (after) ? &(after)->member : NULL, &(n)->member)

#define list_first_entry(list, type, member) \
  ((list)->head ? list_entry((list)->head, type, member) : NULL)

#define list_last_entry(list, type, member) \
  ((list)->tail ? list_entry((list)->tail, type, member) : NULL)

#define list_next_entry(n, member) \
  ((n)->member.next ? list_entry((n)->member.next, TYPEOF(*(n)), member) : NULL)

#define list_prev_entry(n, member) \
  ((n)->member.prev ? list_entry((n)->member.prev, TYPEOF(*(n)), member) : NULL)

#define list_for_each_entry(it, list, type, member)         \
  for (type *it = list_first_entry(list, type, member); it; \
       it = list_next_entry(it, member))

#define list_for_each_entry_safe(it, list, type, member)          \
  for (type *it = list_first_entry(list, type, member),           \
            *it##_next = it ? list_next_entry(it, member) : NULL; \
       it;                                                        \
       it = it##_next, it##_next = it ? list_next_entry(it, member) : NULL)

#define list_for_each_entry_reverse(it, list, type, member) \
  for (type *it = list_last_entry(list, type, member); it;  \
       it = list_prev_entry(it, member))

#define list_for_each_entry_safe_reverse(it, list, type, member)  \
  for (type *it = list_last_entry(list, type, member),            \
            *it##_next = it ? list_prev_entry(it, member) : NULL; \
       it;                                                        \
       it = it##_next, it##_next = it ? list_prev_entry(it, member) : NULL)

#ifdef __cplusplus
}
#endif

#endif
