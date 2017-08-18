#include "core/bitmap.h"

void bitmap_andnot(bitmap_t *map, const bitmap_t *a, const bitmap_t *b,
                   int size) {
  for (int i = 0; i < size; i++) {
    map[i] = a[i] & ~b[i];
  }
}

void bitmap_xor(bitmap_t *map, const bitmap_t *a, const bitmap_t *b, int size) {
  for (int i = 0; i < size; i++) {
    map[i] = a[i] ^ b[i];
  }
}

void bitmap_or(bitmap_t *map, const bitmap_t *a, const bitmap_t *b, int size) {
  for (int i = 0; i < size; i++) {
    map[i] = a[i] | b[i];
  }
}

void bitmap_and(bitmap_t *map, const bitmap_t *a, const bitmap_t *b, int size) {
  for (int i = 0; i < size; i++) {
    map[i] = a[i] & b[i];
  }
}

int bitmap_equal(const bitmap_t *a, const bitmap_t *b, int size) {
  for (int i = 0; i < size; i++) {
    if (a[i] != b[i]) {
      return 0;
    }
  }
  return 1;
}

int bitmap_any(const bitmap_t *map, int offset, int size) {
  int res = 0;
  for (int i = offset; i < offset + size; i++) {
    res |= map[i];
  }
  return res;
}

int bitmap_test(const bitmap_t *map, int offset, int size) {
  int res = 1;
  for (int i = offset; i < offset + size; i++) {
    res &= map[i];
  }
  return res;
}

void bitmap_copy(bitmap_t *map, const bitmap_t *src, int size) {
  for (int i = 0; i < size; i++) {
    map[i] = src[i];
  }
}

void bitmap_clear(bitmap_t *map, int offset, int size) {
  for (int i = offset; i < offset + size; i++) {
    map[i] = 0;
  }
}

void bitmap_set(bitmap_t *map, int offset, int size) {
  for (int i = offset; i < offset + size; i++) {
    map[i] = 1;
  }
}
