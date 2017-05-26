#ifndef SORT_H
#define SORT_H

#include <stddef.h>

/* returns if a is <= b */
typedef int (*sort_cmp)(const void *, const void *);

/* mergesort implementation, FreeBSD took the mergesort symbol */
void msort_noalloc(void *data, void *tmp, int num, size_t size, sort_cmp cmp);
void msort(void *data, int num, size_t size, sort_cmp cmp);

#endif
