#ifndef MM_HEAP_H
#define MM_HEAP_H

// Min-max heap implementation, based on
// http://www.akira.ruc.dk/~keld/teaching/algoritmedesign_f03/Artikler/02../Atkinson86.pdf

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void *mm_type;
typedef bool (*mm_cmp)(mm_type lhs, mm_type rhs);

bool mm_validate(mm_type *begin, int size, mm_cmp cmp);
void mm_push(mm_type *begin, int size, mm_cmp cmp);
mm_type *mm_find_min(mm_type *begin, int size, mm_cmp cmp);
mm_type *mm_find_max(mm_type *begin, int size, mm_cmp cmp);
void mm_pop_min(mm_type *begin, int size, mm_cmp cmp);
void mm_pop_max(mm_type *begin, int size, mm_cmp cmp);

#ifdef __cplusplus
}
#endif

#endif
