#include "guest/bios/scramble.h"
#include "core/core.h"

#define MIN_CHUNK_SIZE 32
#define MAX_CHUNK_SIZE (2048 * 1024)

#define MAX_SLICES (MAX_CHUNK_SIZE / MIN_CHUNK_SIZE)

static int scramble_init(int n) {
  return n & 0xffff;
}

static int scramble_next(int *seed, int i) {
  unsigned key;
  *seed = (*seed * 2109 + 9273) & 0x7fff;
  key = (*seed + 0xc000) & 0xffff;
  return ((unsigned)i * (unsigned)key) >> 16;
}

static void descramble_chunk(int *seed, uint8_t *dst, const uint8_t *src,
                             int size) {
  CHECK((size % MIN_CHUNK_SIZE) == 0 && size <= MAX_CHUNK_SIZE);

  /* descramble each chunk in MIN_CHUNK_SIZE slices */
  size /= MIN_CHUNK_SIZE;

  /* lookup table maps scrambled slice index to descrambled index */
  int table[MAX_SLICES];

  for (int i = 0; i < size; i++) {
    table[i] = i;
  }

  for (int i = size - 1; i >= 0; i--) {
    int x = scramble_next(seed, i);

    /* swap table index */
    int tmp = table[i];
    table[i] = table[x];
    table[x] = tmp;

    /* write slice out to descrambled index */
    memcpy(dst + MIN_CHUNK_SIZE * table[i], src, MIN_CHUNK_SIZE);
    src += MIN_CHUNK_SIZE;
  }
}

void descramble(uint8_t *dst, const uint8_t *src, int size) {
  int seed = scramble_init(size);

  /* descramble the data starting with the largest chunk size (2mb) */
  int chunk_size = MAX_CHUNK_SIZE;

  while (chunk_size >= MIN_CHUNK_SIZE) {
    /* continue descrambling with the current chunk size until the remaining
       data is too small */
    while (size >= chunk_size) {
      descramble_chunk(&seed, dst, src, chunk_size);
      size -= chunk_size;
      dst += chunk_size;
      src += chunk_size;
    }

    /* attempt to use the the next smallest chunk size */
    chunk_size >>= 1;
  }

  /* any remaining data isn't scrambled, just copy it */
  if (size) {
    memcpy(dst, src, size);
  }
}
