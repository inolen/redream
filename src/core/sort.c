#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "core/sort.h"

static void merge(uint8_t *in, uint8_t *out, size_t size, int l, int m, int r,
                  sort_cmp cmp) {
  int i = l;
  int j = m;
  int k = l;

  while (k < r) {
    if (i < m && ((j >= r) || cmp(in + i * size, in + j * size))) {
      memcpy(out + k * size, in + i * size, size);
      k++;
      i++;
    } else {
      memcpy(out + k * size, in + j * size, size);
      k++;
      j++;
    }
  }
}

static void msort_r(uint8_t *in, uint8_t *out, size_t size, int l, int r,
                    sort_cmp cmp) {
  if ((r - l) < 2) {
    return;
  }

  int m = (l + r) / 2;
  msort_r(out, in, size, l, m, cmp);
  msort_r(out, in, size, m, r, cmp);
  merge(in, out, size, l, m, r, cmp);
}

void msort_noalloc(void *data, void *tmp, int num, size_t size, sort_cmp cmp) {
  memcpy(tmp, data, num * size);
  msort_r(tmp, data, size, 0, num, cmp);
}

void msort(void *data, int num, size_t size, sort_cmp cmp) {
  void *tmp = malloc(num * size);
  msort_noalloc(data, tmp, num, size, cmp);
  free(tmp);
}
