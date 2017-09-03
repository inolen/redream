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

/* texture data is loaded into CORE as 8-bit values for r, g, b and a

   in the case of twiddled textures, the deficiency in bits is made up for by
   appending the high-order bits of the color into the low-order bits to make
   a complete 8 bit value. for example:

   src color (6 bit):   internal color (8 bit):
   --------------------------------------------
   c5,c4,c3,c2,c1,c0    c5,c4,c3,c2,c1,c0,c5,c4

   src color (5 bit):   internal color (8 bit):
   --------------------------------------------
   c4,c3,c2,c1,c0       c4,c3,c2,c1,c0,c4,c3,c2

   src color (1 bit):   internal color (8 bit):
   --------------------------------------------
   c0                   c0,c0,c0,c0,c0,c0,c0,c0

   in the case of non-twiddled textures, the colors are zero-extended to make a
   complete 8 bit value. however, when there is only 1 bit the bit is repeated
   in the same way it is for twiddled textures */
#define COLOR_EXTEND_1(c) ((int8_t)(c) >> 7)
#define COLOR_EXTEND_4(c) ((c) | ((c) >> 4))
#define COLOR_EXTEND_5(c) ((c) | ((c) >> 5))
#define COLOR_EXTEND_6(c) ((c) | ((c) >> 6))

/*
 * pixel formats
 */

/* ARGB1555 */
typedef uint16_t ARGB1555_type;
enum { ARGB1555_el = 1 };

#define ARGB1555_UNPACK_A(px) (((px)&0b1000000000000000) >> 8)
#define ARGB1555_UNPACK_R(px) (((px)&0b0111110000000000) >> 7)
#define ARGB1555_UNPACK_G(px) (((px)&0b0000001111100000) >> 2)
#define ARGB1555_UNPACK_B(px) (((px)&0b0000000000011111) << 3)

static inline void ARGB1555_unpack(const ARGB1555_type *src, uint8_t *r,
                                   uint8_t *g, uint8_t *b, uint8_t *a) {
  *a = COLOR_EXTEND_1(ARGB1555_UNPACK_A(src[0]));
  *r = ARGB1555_UNPACK_R(src[0]);
  *g = ARGB1555_UNPACK_G(src[0]);
  *b = ARGB1555_UNPACK_B(src[0]);
}

static inline void ARGB1555_extend(const ARGB1555_type *src, uint8_t *r,
                                   uint8_t *g, uint8_t *b, uint8_t *a) {
  *a = COLOR_EXTEND_1(ARGB1555_UNPACK_A(src[0]));
  *r = COLOR_EXTEND_5(ARGB1555_UNPACK_R(src[0]));
  *g = COLOR_EXTEND_5(ARGB1555_UNPACK_G(src[0]));
  *b = COLOR_EXTEND_5(ARGB1555_UNPACK_B(src[0]));
}

static inline void ARGB1555_pack(ARGB1555_type *dst, uint8_t r, uint8_t g,
                                 uint8_t b, uint8_t a) {
  LOG_FATAL("ARGB1555_pack unsupported");
}

/* RGB565 */
typedef uint16_t RGB565_type;
enum { RGB565_el = 1 };

#define RGB565_UNPACK_R(px) (((px)&0b1111100000000000) >> 8)
#define RGB565_UNPACK_G(px) (((px)&0b0000011111100000) >> 3)
#define RGB565_UNPACK_B(px) (((px)&0b0000000000011111) << 3)

static inline void RGB565_unpack(const RGB565_type *src, uint8_t *r, uint8_t *g,
                                 uint8_t *b, uint8_t *a) {
  *r = RGB565_UNPACK_R(src[0]);
  *g = RGB565_UNPACK_G(src[0]);
  *b = RGB565_UNPACK_B(src[0]);
  *a = 0xff;
}

static inline void RGB565_extend(const RGB565_type *src, uint8_t *r, uint8_t *g,
                                 uint8_t *b, uint8_t *a) {
  *r = COLOR_EXTEND_5(RGB565_UNPACK_R(src[0]));
  *g = COLOR_EXTEND_6(RGB565_UNPACK_G(src[0]));
  *b = COLOR_EXTEND_5(RGB565_UNPACK_B(src[0]));
  *a = 0xff;
}

static inline void RGB565_pack(RGB565_type *dst, uint8_t r, uint8_t g,
                               uint8_t b, uint8_t a) {
  LOG_FATAL("RGB565_pack unsupported");
}

/* UYVY422 */
typedef uint16_t UYVY422_type;
enum { UYVY422_el = 2 };

static inline uint8_t UYVY422_UNPACK_R(int y, int u, int v) {
  int r = y + (11 * v) / 8;
  return MAX(0, MIN(255, r));
}

static inline uint8_t UYVY422_UNPACK_G(int y, int u, int v) {
  int g = y - (11 * u + 22 * v) / 32;
  return MAX(0, MIN(255, g));
}

static inline uint8_t UYVY422_UNPACK_B(int y, int u, int v) {
  int b = y + (55 * u) / 32;
  return MAX(0, MIN(255, b));
}

static inline void UYVY422_unpack(const UYVY422_type *src, uint8_t *r,
                                  uint8_t *g, uint8_t *b, uint8_t *a) {
  int u = (int)(src[0] & 0xff) - 128;
  int y0 = (int)((src[0] >> 8) & 0xff);
  int v = (int)((src[1] & 0xff) & 0xff) - 128;
  int y1 = (int)((src[1] >> 8) & 0xff);
  r[0] = UYVY422_UNPACK_R(y0, u, v);
  g[0] = UYVY422_UNPACK_G(y0, u, v);
  b[0] = UYVY422_UNPACK_B(y0, u, v);
  a[0] = 0xff;
  r[1] = UYVY422_UNPACK_R(y1, u, v);
  g[1] = UYVY422_UNPACK_G(y1, u, v);
  b[1] = UYVY422_UNPACK_B(y1, u, v);
  a[1] = 0xff;
}

static inline void UYVY422_extend(const UYVY422_type *src, uint8_t *r,
                                  uint8_t *g, uint8_t *b, uint8_t *a) {
  UYVY422_unpack(src, r, g, b, a);
}

static inline void UYVY422_pack(UYVY422_type *dst, uint8_t r, uint8_t g,
                                uint8_t b, uint8_t a) {
  LOG_FATAL("UYVY422_pack unsupported");
}

/* ARGB4444 */
typedef uint16_t ARGB4444_type;
enum { ARGB4444_el = 1 };

#define ARGB4444_UNPACK_A(px) (((px)&0b1111000000000000) >> 8)
#define ARGB4444_UNPACK_R(px) (((px)&0b0000111100000000) >> 4)
#define ARGB4444_UNPACK_G(px) (((px)&0b0000000011110000) << 0)
#define ARGB4444_UNPACK_B(px) (((px)&0b0000000000001111) << 4)

static inline void ARGB4444_unpack(const ARGB4444_type *src, uint8_t *r,
                                   uint8_t *g, uint8_t *b, uint8_t *a) {
  *a = ARGB4444_UNPACK_A(src[0]);
  *r = ARGB4444_UNPACK_R(src[0]);
  *g = ARGB4444_UNPACK_G(src[0]);
  *b = ARGB4444_UNPACK_B(src[0]);
}

static inline void ARGB4444_extend(const ARGB4444_type *src, uint8_t *r,
                                   uint8_t *g, uint8_t *b, uint8_t *a) {
  *a = COLOR_EXTEND_4(ARGB4444_UNPACK_A(src[0]));
  *r = COLOR_EXTEND_4(ARGB4444_UNPACK_R(src[0]));
  *g = COLOR_EXTEND_4(ARGB4444_UNPACK_G(src[0]));
  *b = COLOR_EXTEND_4(ARGB4444_UNPACK_B(src[0]));
}

static inline void ARGB4444_pack(ARGB4444_type *dst, uint8_t r, uint8_t g,
                                 uint8_t b, uint8_t a) {
  LOG_FATAL("ARGB4444_pack unsupported");
}

/* ARGB8888 */
typedef uint32_t ARGB8888_type;
enum { ARGB8888_el = 1 };

#define ARGB8888_UNPACK_A(px) (((px) >> 24) & 0xff)
#define ARGB8888_UNPACK_R(px) (((px) >> 16) & 0xff)
#define ARGB8888_UNPACK_G(px) (((px) >> 8) & 0xff)
#define ARGB8888_UNPACK_B(px) ((px)&0xff)

static inline void ARGB8888_unpack(const ARGB8888_type *src, uint8_t *r,
                                   uint8_t *g, uint8_t *b, uint8_t *a) {
  *a = ARGB8888_UNPACK_A(src[0]);
  *r = ARGB8888_UNPACK_R(src[0]);
  *g = ARGB8888_UNPACK_G(src[0]);
  *b = ARGB8888_UNPACK_B(src[0]);
}

static inline void ARGB8888_extend(const ARGB8888_type *src, uint8_t *r,
                                   uint8_t *g, uint8_t *b, uint8_t *a) {
  ARGB8888_unpack(src, r, g, b, a);
}

static inline void ARGB8888_pack(ARGB8888_type *dst, uint8_t r, uint8_t g,
                                 uint8_t b, uint8_t a) {
  LOG_FATAL("ARGB8888_pack unsupported");
}

/* RGBA */
typedef uint32_t RGBA_type;
enum { RGBA_el = 1 };

static inline void RGBA_unpack(const RGBA_type *src, uint8_t *r, uint8_t *g,
                               uint8_t *b, uint8_t *a) {
  LOG_FATAL("RGBA_unpack unsupported");
}

static inline void RGBA_extend(const RGBA_type *src, uint8_t *r, uint8_t *g,
                               uint8_t *b, uint8_t *a) {
  RGBA_unpack(src, r, g, b, a);
}

static inline void RGBA_pack(RGBA_type *dst, uint8_t r, uint8_t g, uint8_t b,
                             uint8_t a) {
  uint8_t *dst_arr = (uint8_t *)dst;
  dst_arr[0] = r;
  dst_arr[1] = g;
  dst_arr[2] = b;
  dst_arr[3] = a;
}

/*
 * texture formats
 */

#define define_convert_planar(FROM, TO)                                      \
  static inline void convert_planar_##FROM##_##TO(const FROM##_type *src,    \
                                                  TO##_type *dst, int width, \
                                                  int height, int stride) {  \
    uint8_t r[FROM##_el];                                                    \
    uint8_t g[FROM##_el];                                                    \
    uint8_t b[FROM##_el];                                                    \
    uint8_t a[FROM##_el];                                                    \
                                                                             \
    for (int y = 0; y < height; y++) {                                       \
      const FROM##_type *end = src + stride;                                 \
      while (src < end) {                                                    \
        FROM##_unpack(src, r, g, b, a);                                      \
        for (int i = 0; i < FROM##_el; i++) {                                \
          TO##_pack(dst++, r[i], g[i], b[i], a[i]);                          \
        }                                                                    \
        src += FROM##_el;                                                    \
      }                                                                      \
      dst += (width - stride);                                               \
    }                                                                        \
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
        FROM##_extend(tmp, r, g, b, a);                                \
        for (int i = 0; i < FROM##_el; i++) {                          \
          TO##_pack(dst++, r[i], g[i], b[i], a[i]);                    \
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
        FROM##_extend(entry, &r, &g, &b, &a);                                 \
        TO##_pack(dst++, r, g, b, a);                                         \
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
        FROM##_extend(entry, &r, &g, &b, &a);                                 \
        TO##_pack(dst++, r, g, b, a);                                         \
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
        FROM##_extend(code, &r, &g, &b, &a);                                \
        TO##_pack(dst++, r, g, b, a);                                       \
      }                                                                     \
    }                                                                       \
  }

define_convert_planar(ARGB1555, RGBA);
define_convert_planar(RGB565, RGBA);
define_convert_planar(UYVY422, RGBA);
define_convert_planar(ARGB4444, RGBA);

define_convert_twiddled(ARGB1555, RGBA);
define_convert_twiddled(RGB565, RGBA);
define_convert_twiddled(UYVY422, RGBA);
define_convert_twiddled(ARGB4444, RGBA);

define_convert_pal4(ARGB1555, RGBA);
define_convert_pal4(RGB565, RGBA);
define_convert_pal4(ARGB4444, RGBA);
define_convert_pal4(ARGB8888, RGBA);

define_convert_pal8(ARGB1555, RGBA);
define_convert_pal8(RGB565, RGBA);
define_convert_pal8(ARGB4444, RGBA);
define_convert_pal8(ARGB8888, RGBA);

define_convert_vq(ARGB1555, RGBA);
define_convert_vq(RGB565, RGBA);
define_convert_vq(ARGB4444, RGBA);

#endif
