#ifndef BITMAP_H
#define BITMAP_H

#include <stdint.h>

#if 0
typedef uint64_t bitmap_t;

#define BITMAP_BITS_PER_WORD (sizeof(bitmap_t) * 8)
#define BITMAP_WORDS_TO_BITS(n) (n * BITMAP_BITS_PER_WORD)
#define BITMAP_BITS_TO_WORDS(n) \
  ((n + BITMAP_BITS_PER_WORD - 1) / BITMAP_BITS_PER_WORD)
#define DECLARE_BITMAP(name, bits) bitmap_t name[BITMAP_BITS_TO_WORDS(bits)]
#else
typedef uint8_t bitmap_t;

#define DECLARE_BITMAP(name, bits) bitmap_t name[bits]
#endif

void bitmap_set(bitmap_t *map, int offset, int size);
void bitmap_clear(bitmap_t *map, int offset, int size);
void bitmap_copy(bitmap_t *map, const bitmap_t *src, int size);

int bitmap_test(const bitmap_t *map, int offset, int size);
int bitmap_any(const bitmap_t *map, int offset, int size);
int bitmap_equal(const bitmap_t *a, const bitmap_t *b, int size);

void bitmap_and(bitmap_t *map, const bitmap_t *a, const bitmap_t *b, int size);
void bitmap_or(bitmap_t *map, const bitmap_t *a, const bitmap_t *b, int size);
void bitmap_xor(bitmap_t *map, const bitmap_t *a, const bitmap_t *b, int size);
void bitmap_andnot(bitmap_t *map, const bitmap_t *a, const bitmap_t *b,
                   int size);

#endif
