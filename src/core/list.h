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

void list_add(list_t *list, list_node_t *n);
void list_add_after(list_t *list, list_node_t *after, list_node_t *n);
void list_remove(list_t *list, list_node_t *n);
void list_clear(list_t *list);
int list_empty(list_t *list);

#define list_for_each(list, it)                                               \
  for (list_node_t *it = (list)->head, *it##_next = it ? it->next : NULL; it; \
       it = it##_next, it##_next = it ? it->next : NULL)

#define list_entry(n, type, member) container_of(n, type, member)

#define list_first_entry(list, type, member) \
  ((list)->head ? list_entry((list)->head, type, member) : NULL)

#define list_last_entry(list, type, member) \
  ((list)->tail ? list_entry((list)->tail, type, member) : NULL)

#ifdef __cplusplus

#define list_next_entry(n, member)                                       \
  ((n) && (n)->member.next                                               \
       ? list_entry((n)->member.next,                                    \
                    std::remove_reference<decltype(*(n))>::type, member) \
       : NULL)

#define list_prev_entry(n, member)                                       \
  ((n) && (n)->member.prev                                               \
       ? list_entry((n)->member.prev,                                    \
                    std::remove_reference<decltype(*(n))>::type, member) \
       : NULL)

#else

#define list_next_entry(n, member)                              \
  ((n) && (n)->member.next                                      \
       ? list_entry((n)->member.next, __typeof__(*(n)), member) \
       : NULL)

#define list_prev_entry(n, member)                              \
  ((n) && (n)->member.prev                                      \
       ? list_entry((n)->member.prev, __typeof__(*(n)), member) \
       : NULL)

#endif

#define list_for_each_entry(list, type, member, it)     \
  for (type *it = list_first_entry(list, type, member), \
            *it##_next = list_next_entry(it, member);   \
       it; it = it##_next, it##_next = list_next_entry(it, member))

#define list_for_each_entry_reverse(list, type, member, it) \
  for (type *it = list_last_entry(list, type, member),      \
            *it##_next = list_prev_entry(it, member);       \
       it; it = it##_next, it##_next = list_prev_entry(it, member))

#ifdef __cplusplus
}
#endif

#endif
