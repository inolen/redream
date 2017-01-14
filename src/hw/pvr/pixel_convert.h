#ifndef PIXEL_CONVERT_H
#define PIXEL_CONVERT_H

#include "core/math.h"

/* helper functions for converting between different pixel formats */

#define TWIDTAB(x)                                                          \
  (((x)&1) | (((x)&2) << 1) | (((x)&4) << 2) | (((x)&8) << 3) |             \
   (((x)&16) << 4) | (((x)&32) << 5) | (((x)&64) << 6) | (((x)&128) << 7) | \
   (((x)&256) << 8) | (((x)&512) << 9))
#define TWIDIDX(x, y, min)                                        \
  (((TWIDTAB((x) & ((min)-1)) << 1) | TWIDTAB((y) & ((min)-1))) + \
   ((x) / (min) + (y) / (min)) * (min) * (min))

/* ARGB1555 */
typedef uint16_t ARGB1555_type;
enum { ARGB1555_el = 1 };

static inline void ARGB1555_read(const ARGB1555_type *px, uint8_t *r,
                                 uint8_t *g, uint8_t *b, uint8_t *a) {
  *a = (px[0] & 0b1000000000000000) >> 8;
  *r = (px[0] & 0b0111110000000000) >> 7;
  *g = (px[0] & 0b0000001111100000) >> 2;
  *b = (px[0] & 0b0000000000011111) << 3;
}

static inline void ARGB1555_write(ARGB1555_type *dst, uint8_t r, uint8_t g,
                                  uint8_t b, uint8_t a) {
  *dst = ((a >> 7) << 15) | ((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3);
}

/* RGBA5551 */
typedef uint16_t RGBA5551_type;
enum { RGBA5551_el = 1 };

static inline void RGBA5551_read(const RGBA5551_type *px, uint8_t *r,
                                 uint8_t *g, uint8_t *b, uint8_t *a) {
  *r = (px[0] & 0b1111100000000000) >> 8;
  *g = (px[0] & 0b0000011111000000) >> 3;
  *b = (px[0] & 0b0000000000111110) << 2;
  *a = (px[0] & 0b0000000000000001) << 7;
}

static inline void RGBA5551_write(RGBA5551_type *dst, uint8_t r, uint8_t g,
                                  uint8_t b, uint8_t a) {
  *dst = ((r >> 3) << 11) | ((g >> 3) << 6) | ((b >> 3) << 1) | (a >> 7);
}

/* RGB565 */
typedef uint16_t RGB565_type;
enum { RGB565_el = 1 };

static inline void RGB565_read(const RGB565_type *px, uint8_t *r, uint8_t *g,
                               uint8_t *b, uint8_t *a) {
  *r = (px[0] & 0b1111100000000000) >> 8;
  *g = (px[0] & 0b0000011111100000) >> 3;
  *b = (px[0] & 0b0000000000011111) << 3;
  *a = 0xff;
}

static inline void RGB565_write(RGB565_type *dst, uint8_t r, uint8_t g,
                                uint8_t b, uint8_t a) {
  *dst = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
}

/* UYVY422 */
typedef uint16_t UYVY422_type;
enum { UYVY422_el = 2 };

static inline uint8_t yuv_to_r(int y, int u, int v) {
  int r = y + (11 * v) / 8;
  return MAX(0, MIN(255, r));
}

static inline uint8_t yuv_to_g(int y, int u, int v) {
  int g = y - (11 * u + 22 * v) / 32;
  return MAX(0, MIN(255, g));
}

static inline uint8_t yuv_to_b(int y, int u, int v) {
  int b = y + (55 * u) / 32;
  return MAX(0, MIN(255, b));
}

static inline void UYVY422_read(const UYVY422_type *px, uint8_t *r, uint8_t *g,
                                uint8_t *b, uint8_t *a) {
  int u = (int)(px[0] & 0xff) - 128;
  int y0 = (int)((px[0] >> 8) & 0xff);
  int v = (int)((px[1] & 0xff) & 0xff) - 128;
  int y1 = (int)((px[1] >> 8) & 0xff);
  r[0] = yuv_to_r(y0, u, v);
  g[0] = yuv_to_g(y0, u, v);
  b[0] = yuv_to_b(y0, u, v);
  r[1] = yuv_to_r(y1, u, v);
  g[1] = yuv_to_g(y1, u, v);
  b[1] = yuv_to_b(y1, u, v);
}

static inline void UYVY422_write(UYVY422_type *dst, uint8_t r, uint8_t g,
                                 uint8_t b, uint8_t a) {
  LOG_FATAL("UYVY422_write unsupported");
}

/* ARGB4444 */
typedef uint16_t ARGB4444_type;
enum { ARGB4444_el = 1 };

static inline void ARGB4444_read(const ARGB4444_type *px, uint8_t *r,
                                 uint8_t *g, uint8_t *b, uint8_t *a) {
  *a = (px[0] & 0b1111000000000000) >> 8;
  *r = (px[0] & 0b0000111100000000) >> 4;
  *g = (px[0] & 0b0000000011110000) << 0;
  *b = (px[0] & 0b0000000000001111) << 4;
}

static inline void ARGB4444_write(ARGB4444_type *dst, uint8_t r, uint8_t g,
                                  uint8_t b, uint8_t a) {
  *dst = ((a >> 4) << 12) | ((r >> 4) << 8) | ((g >> 4) << 4) | (b >> 4);
}

/* RGBA4444 */
typedef uint16_t RGBA4444_type;
enum { RGBA4444_el = 1 };

static inline void RGBA4444_read(const RGBA4444_type *px, uint8_t *r,
                                 uint8_t *g, uint8_t *b, uint8_t *a) {
  *r = (px[0] & 0b1111000000000000) >> 8;
  *g = (px[0] & 0b0000111100000000) >> 4;
  *b = (px[0] & 0b0000000011110000) << 0;
  *a = (px[0] & 0b0000000000001111) << 4;
}

static inline void RGBA4444_write(RGBA4444_type *dst, uint8_t r, uint8_t g,
                                  uint8_t b, uint8_t a) {
  *dst = ((r >> 4) << 12) | ((g >> 4) << 8) | ((b >> 4) << 4) | (a >> 4);
}

/* ARGB8888 */
typedef uint32_t ARGB8888_type;
enum { ARGB8888_el = 1 };

static inline void ARGB8888_read(const ARGB8888_type *px, uint8_t *r,
                                 uint8_t *g, uint8_t *b, uint8_t *a) {
  *a = (px[0] >> 24) & 0xff;
  *r = (px[0] >> 16) & 0xff;
  *g = (px[0] >> 8) & 0xff;
  *b = px[0] & 0xff;
}

static inline void ARGB8888_write(ARGB8888_type *dst, uint8_t r, uint8_t g,
                                  uint8_t b, uint8_t a) {
  *dst = (a << 24) | (r << 16) | (g << 8) | b;
}

/* RGBA8888 */
typedef uint32_t RGBA8888_type;
enum { RGBA8888_el = 1 };

static inline void RGBA8888_read(const RGBA8888_type *px, uint8_t *r,
                                 uint8_t *g, uint8_t *b, uint8_t *a) {
  *r = (px[0] >> 24) & 0xff;
  *g = (px[0] >> 16) & 0xff;
  *b = (px[0] >> 8) & 0xff;
  *a = px[0] & 0xff;
}

static inline void RGBA8888_write(RGBA8888_type *dst, uint8_t r, uint8_t g,
                                  uint8_t b, uint8_t a) {
  *dst = (r << 24) | (g << 16) | (b << 8) | a;
}

#define define_convert(FROM, TO)                                       \
  static inline void convert_##FROM##_##TO(const FROM##_type *src,     \
                                           TO##_type *dst, int width,  \
                                           int height, int stride) {   \
    uint8_t r[FROM##_el];                                              \
    uint8_t g[FROM##_el];                                              \
    uint8_t b[FROM##_el];                                              \
    uint8_t a[FROM##_el];                                              \
                                                                       \
    for (int y = 0; y < height; y++) {                                 \
      for (int x = 0; x < stride; x += FROM##_el) {                    \
        FROM##_read(&src[y * stride + x], r, g, b, a);                 \
        for (int i = 0; i < FROM##_el; i++) {                          \
          TO##_write(&dst[y * width + x + i], r[i], g[i], b[i], a[i]); \
        }                                                              \
      }                                                                \
    }                                                                  \
  }

#define define_convert_twiddled(FROM, TO)                              \
  static inline void convert_twiddled_##FROM##_##TO(                   \
      const FROM##_type *src, TO##_type *dst, int width, int height) { \
    int min = MIN(width, height);                                      \
    uint8_t r[FROM##_el];                                              \
    uint8_t g[FROM##_el];                                              \
    uint8_t b[FROM##_el];                                              \
    uint8_t a[FROM##_el];                                              \
                                                                       \
    /* multi-element source data will not be contiguous in memory      \
       when twiddled, so a temp buffer copy is required */             \
    FROM##_type tmp[FROM##_el];                                        \
                                                                       \
    for (int y = 0; y < height; y++) {                                 \
      for (int x = 0; x < width; x += FROM##_el) {                     \
        for (int i = 0; i < FROM##_el; i++) {                          \
          tmp[i] = src[TWIDIDX(x + i, y, min)];                        \
        }                                                              \
        FROM##_read(tmp, r, g, b, a);                                  \
        for (int i = 0; i < FROM##_el; i++) {                          \
          TO##_write(dst++, r[i], g[i], b[i], a[i]);                   \
        }                                                              \
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
    for (int y = 0; y < height; y++) {                                        \
      for (int x = 0; x < width; x++) {                                       \
        int twid_idx = TWIDIDX(x, y, min);                                    \
        int pal_idx = src[twid_idx >> 1];                                     \
        if (twid_idx & 1) {                                                   \
          pal_idx >>= 4;                                                      \
        } else {                                                              \
          pal_idx &= 0xf;                                                     \
        }                                                                     \
        const FROM##_type *entry = (const FROM##_type *)&palette[pal_idx];    \
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
    for (int y = 0; y < height; y++) {                                        \
      for (int x = 0; x < width; x++) {                                       \
        int pal_idx = src[TWIDIDX(x, y, min)];                                \
        const FROM##_type *entry = (const FROM##_type *)&palette[pal_idx];    \
        FROM##_read(entry, &r, &g, &b, &a);                                   \
        TO##_write(dst++, r, g, b, a);                                        \
      }                                                                       \
    }                                                                         \
  }

#define define_convert_vq(FROM, TO)                                         \
  static inline void convert_vq_##FROM##_##TO(                              \
      const uint8_t *codebook, const uint8_t *index, TO##_type *dst,        \
      int width, int height) {                                              \
    int min = MIN(width, height);                                           \
    uint8_t r, g, b, a;                                                     \
                                                                            \
    /* always twiddled */                                                   \
    for (int y = 0; y < height; y++) {                                      \
      for (int x = 0; x < width; x++) {                                     \
        int twid_idx = TWIDIDX(x, y, min);                                  \
        int code_idx = index[twid_idx / 4] * 8 + ((twid_idx % 4) * 2);      \
        const FROM##_type *code = (const FROM##_type *)&codebook[code_idx]; \
        FROM##_read(code, &r, &g, &b, &a);                                  \
        TO##_write(dst++, r, g, b, a);                                      \
      }                                                                     \
    }                                                                       \
  }

define_convert(ARGB1555, RGBA5551);
define_convert(RGB565, RGB565);
define_convert(UYVY422, RGB565);
define_convert(ARGB4444, RGBA4444);

define_convert_twiddled(ARGB1555, RGBA5551);
define_convert_twiddled(RGB565, RGB565);
define_convert_twiddled(UYVY422, RGB565);
define_convert_twiddled(ARGB4444, RGBA4444);

define_convert_pal4(ARGB1555, RGBA5551);
define_convert_pal4(RGB565, RGB565);
define_convert_pal4(ARGB4444, RGBA4444);
define_convert_pal4(ARGB8888, RGBA8888);

define_convert_pal8(ARGB1555, RGBA5551);
define_convert_pal8(RGB565, RGB565);
define_convert_pal8(ARGB4444, RGBA4444);
define_convert_pal8(ARGB8888, RGBA8888);

define_convert_vq(ARGB1555, RGBA5551);
define_convert_vq(RGB565, RGB565);
define_convert_vq(ARGB4444, RGBA4444);

#endif
