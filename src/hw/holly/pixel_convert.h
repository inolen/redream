#ifndef PIXEL_CONVERT_H
#define PIXEL_CONVERT_H

#include "core/math.h"

// Helper class for converting between different pixel formats.
// Please note that RGBA values aren't normalized in the read and write
// operations, so conversions between different bit widths aren't supported.
// Also to note, Dreamcast palette data, regardless of the format stored, is
// bytes per entry.

#define TWIDTAB(x)                                                          \
  ((x & 1) | ((x & 2) << 1) | ((x & 4) << 2) | ((x & 8) << 3) |             \
   ((x & 16) << 4) | ((x & 32) << 5) | ((x & 64) << 6) | ((x & 128) << 7) | \
   ((x & 256) << 8) | ((x & 512) << 9))
#define TWIDIDX(x, y, min)                                    \
  (((TWIDTAB(x & (min - 1)) << 1) | TWIDTAB(y & (min - 1))) + \
   (x / min + y / min) * min * min)

typedef uint16_t ARGB1555_type;

static inline void ARGB1555_read(ARGB1555_type px, uint8_t *r, uint8_t *g,
                                 uint8_t *b, uint8_t *a) {
  *a = (px >> 15) & 0x1;
  *r = (px >> 10) & 0x1f;
  *g = (px >> 5) & 0x1f;
  *b = px & 0x1f;
}

static inline void ARGB1555_write(ARGB1555_type *dst, uint8_t r, uint8_t g,
                                  uint8_t b, uint8_t a) {
  *dst =
      ((a & 0x1) << 15) | ((r & 0x1f) << 10) | ((g & 0x1f) << 5) | (b & 0x1f);
}

typedef uint16_t RGBA5551_type;

static inline void RGBA5551_read(RGBA5551_type px, uint8_t *r, uint8_t *g,
                                 uint8_t *b, uint8_t *a) {
  *r = (px >> 11) & 0x1f;
  *g = (px >> 6) & 0x1f;
  *b = (px >> 1) & 0x1f;
  *a = px & 0x1;
}

static inline void RGBA5551_write(RGBA5551_type *dst, uint8_t r, uint8_t g,
                                  uint8_t b, uint8_t a) {
  *dst = ((r & 0x1f) << 11) | ((g & 0x1f) << 6) | ((b & 0x1f) << 1) | (a & 0x1);
}

typedef uint16_t RGB565_type;

static inline void RGB565_read(RGB565_type px, uint8_t *r, uint8_t *g,
                               uint8_t *b, uint8_t *a) {
  *r = (px >> 11) & 0x1f;
  *g = (px >> 5) & 0x3f;
  *b = px & 0x1f;
  *a = 0xff;
}

static inline void RGB565_write(RGB565_type *dst, uint8_t r, uint8_t g,
                                uint8_t b, uint8_t a) {
  *dst = ((r & 0x1f) << 11) | ((g & 0x3f) << 5) | (b & 0x1f);
}

typedef uint16_t ARGB4444_type;

static inline void ARGB4444_read(ARGB4444_type px, uint8_t *r, uint8_t *g,
                                 uint8_t *b, uint8_t *a) {
  *a = (px >> 12) & 0xf;
  *r = (px >> 8) & 0xf;
  *g = (px >> 4) & 0xf;
  *b = px & 0xf;
}

static inline void ARGB4444_write(ARGB4444_type *dst, uint8_t r, uint8_t g,
                                  uint8_t b, uint8_t a) {
  *dst = ((a & 0xf) << 12) | ((r & 0xf) << 8) | ((g & 0xf) << 4) | (b & 0xf);
}

typedef uint16_t RGBA4444_type;

static inline void RGBA4444_read(RGBA4444_type px, uint8_t *r, uint8_t *g,
                                 uint8_t *b, uint8_t *a) {
  *r = (px >> 12) & 0xf;
  *g = (px >> 8) & 0xf;
  *b = (px >> 4) & 0xf;
  *a = px & 0xf;
}

static inline void RGBA4444_write(RGBA4444_type *dst, uint8_t r, uint8_t g,
                                  uint8_t b, uint8_t a) {
  *dst = ((r & 0xf) << 12) | ((g & 0xf) << 8) | ((b & 0xf) << 4) | (a & 0xf);
}

typedef uint32_t ARGB8888_type;

static inline void ARGB8888_read(ARGB8888_type px, uint8_t *r, uint8_t *g,
                                 uint8_t *b, uint8_t *a) {
  *a = (px >> 24) & 0xff;
  *r = (px >> 16) & 0xff;
  *g = (px >> 8) & 0xff;
  *b = px & 0xff;
}

static inline void ARGB8888_write(ARGB8888_type *dst, uint8_t r, uint8_t g,
                                  uint8_t b, uint8_t a) {
  *dst = (a << 24) | (r << 16) | (g << 8) | b;
}

typedef uint32_t RGBA8888_type;

static inline void RGBA8888_read(RGBA8888_type px, uint8_t *r, uint8_t *g,
                                 uint8_t *b, uint8_t *a) {
  *r = (px >> 24) & 0xff;
  *g = (px >> 16) & 0xff;
  *b = (px >> 8) & 0xff;
  *a = px & 0xff;
}

static inline void RGBA8888_write(RGBA8888_type *dst, uint8_t r, uint8_t g,
                                  uint8_t b, uint8_t a) {
  *dst = (r << 24) | (g << 16) | (b << 8) | a;
}

#define define_convert(FROM, TO)                                       \
  static inline void convert_##FROM##_##TO(                            \
      const FROM##_type *src, TO##_type *dst, int width, int height) { \
    uint8_t r, g, b, a;                                                \
                                                                       \
    for (int y = 0; y < height; ++y) {                                 \
      for (int x = 0; x < width; ++x) {                                \
        FROM##_read(*(src++), &r, &g, &b, &a);                         \
        TO##_write(dst++, r, g, b, a);                                 \
      }                                                                \
    }                                                                  \
  }

#define define_convert_twiddled(FROM, TO)                              \
  static inline void convert_twiddled_##FROM##_##TO(                   \
      const FROM##_type *src, TO##_type *dst, int width, int height) { \
    int min = MIN(width, height);                                      \
    uint8_t r, g, b, a;                                                \
                                                                       \
    for (int y = 0; y < height; ++y) {                                 \
      for (int x = 0; x < width; ++x) {                                \
        int tidx = TWIDIDX(x, y, min);                                 \
        FROM##_read(src[tidx], &r, &g, &b, &a);                        \
        TO##_write(dst++, r, g, b, a);                                 \
      }                                                                \
    }                                                                  \
  }

#define define_convert_pal4(FROM, TO)                                         \
  static inline void convert_pal4_##FROM##_##TO(                              \
      const uint8_t *src, TO##_type *dst, const uint32_t *palette, int width, \
      int height) {                                                           \
    int min = MIN(width, height);                                             \
    uint8_t r, g, b, a;                                                       \
                                                                              \
    /* always twiddled */                                                     \
    for (int y = 0; y < height; ++y) {                                        \
      for (int x = 0; x < width; ++x) {                                       \
        int tidx = TWIDIDX(x, y, min);                                        \
        int palette_idx = src[tidx >> 1];                                     \
        if (tidx & 1) {                                                       \
          palette_idx >>= 4;                                                  \
        } else {                                                              \
          palette_idx &= 0xf;                                                 \
        }                                                                     \
        FROM##_type entry = *(const FROM##_type *)&palette[palette_idx];      \
        FROM##_read(entry, &r, &g, &b, &a);                                   \
        TO##_write(dst++, r, g, b, a);                                        \
      }                                                                       \
    }                                                                         \
  }

#define define_convert_pal8(FROM, TO)                                         \
  static inline void convert_pal8_##FROM##_##TO(                              \
      const uint8_t *src, TO##_type *dst, const uint32_t *palette, int width, \
      int height) {                                                           \
    int min = MIN(width, height);                                             \
    uint8_t r, g, b, a;                                                       \
                                                                              \
    /* always twiddled */                                                     \
    for (int y = 0; y < height; ++y) {                                        \
      for (int x = 0; x < width; ++x) {                                       \
        int palette_idx = src[TWIDIDX(x, y, min)];                            \
        FROM##_type entry = *(FROM##_type *)&palette[palette_idx];            \
        FROM##_read(entry, &r, &g, &b, &a);                                   \
        TO##_write(dst++, r, g, b, a);                                        \
      }                                                                       \
    }                                                                         \
  }

#define define_convert_vq(FROM, TO)                                            \
  static inline void convert_vq_##FROM##_##TO(                                 \
      const uint8_t *codebook, const uint8_t *index, TO##_type *dst,           \
      int width, int height) {                                                 \
    int min = MIN(width, height);                                              \
    uint8_t r, g, b, a;                                                        \
                                                                               \
    /* always twiddled */                                                      \
    for (int y = 0; y < height; ++y) {                                         \
      for (int x = 0; x < width; ++x) {                                        \
        int tidx = TWIDIDX(x, y, min);                                         \
        FROM##_type code =                                                     \
            *(FROM##_type *)&codebook[index[tidx / 4] * 8 + ((tidx % 4) * 2)]; \
        FROM##_read(code, &r, &g, &b, &a);                                     \
        TO##_write(dst++, r, g, b, a);                                         \
      }                                                                        \
    }                                                                          \
  }

define_convert(ARGB1555, RGBA5551);
define_convert(RGB565, RGB565);
define_convert(ARGB4444, RGBA4444);

define_convert_twiddled(ARGB1555, RGBA5551);
define_convert_twiddled(RGB565, RGB565);
define_convert_twiddled(ARGB4444, RGBA4444);

define_convert_pal4(ARGB4444, RGBA4444);

define_convert_pal8(ARGB4444, RGBA4444);
define_convert_pal8(ARGB8888, RGBA8888);

define_convert_vq(ARGB1555, RGBA5551);
define_convert_vq(RGB565, RGB565);
define_convert_vq(ARGB4444, RGBA4444);

#endif
