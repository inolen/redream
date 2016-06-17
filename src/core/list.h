#ifndef REDREAM_LIST_H
#define REDREAM_LIST_H

#include <stdlib.h>
#include "core/core.h"

struct list_node {
  struct list_node *prev;
  struct list_node *next;
};

struct list {
  struct list_node *head;
  struct list_node *tail;
};

typedef int (*list_node_cmp)(const struct list_node *a,
                             const struct list_node *b);

int list_empty(struct list *list);
void list_add(struct list *list, struct list_node *n);
void list_add_after(struct list *list, struct list_node *after,
                    struct list_node *n);
void list_remove(struct list *list, struct list_node *n);
void list_clear(struct list *list);
void list_sort(struct list *list, list_node_cmp cmp);

#define list_for_each(list, it)                                                \
  for (struct list_node *it = (list)->head, *it##_next = it ? it->next : NULL; \
       it; it = it##_next, it##_next = it ? it->next : NULL)

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

#endif
