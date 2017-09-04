#include "guest/pvr/tex.h"
#include "core/assert.h"

/*
 * pixel formats
 */

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
   in the same way it is for twiddled textures

   note, for simplicity in the emulator, non-twiddled textures are extended the
   same as twiddled textures */
static inline uint8_t COLOR_EXTEND_1(uint8_t c) {
  return (int8_t)c >> 7;
}

static inline uint8_t COLOR_EXTEND_4(uint8_t c) {
  return c | (c >> 4);
}

static inline uint8_t COLOR_EXTEND_5(uint8_t c) {
  return c | (c >> 5);
}

static inline uint8_t COLOR_EXTEND_6(uint8_t c) {
  return c | (c >> 6);
}

/* ARGB1555 */
// typedef uint16_t ARGB1555_type;

static inline void ARGB1555_unpack(ARGB1555_type src, uint8_t *rgba) {
  rgba[0] = COLOR_EXTEND_5((src & 0b0111110000000000) >> 7);
  rgba[1] = COLOR_EXTEND_5((src & 0b0000001111100000) >> 2);
  rgba[2] = COLOR_EXTEND_5((src & 0b0000000000011111) << 3);
  rgba[3] = COLOR_EXTEND_1((src & 0b1000000000000000) >> 8);
}

static inline void ARGB1555_unpack_bitmap(const ARGB1555_type *src,
                                          uint8_t *rgba) {
  ARGB1555_unpack(src[0], rgba + 0x0);
  ARGB1555_unpack(src[1], rgba + 0x4);
  ARGB1555_unpack(src[2], rgba + 0x8);
  ARGB1555_unpack(src[3], rgba + 0xc);
}

static inline void ARGB1555_unpack_twiddled(const ARGB1555_type *src,
                                            uint8_t *rgba) {
  ARGB1555_unpack(src[0], rgba + 0x0);
  ARGB1555_unpack(src[1], rgba + 0x4);
  ARGB1555_unpack(src[2], rgba + 0x8);
  ARGB1555_unpack(src[3], rgba + 0xc);
}

static inline void ARGB1555_unpack_pal4(const uint8_t *src, const uint32_t *pal,
                                        uint8_t *rgba) {
  ARGB1555_unpack((ARGB1555_type)pal[src[0] & 15], rgba + 0x0);
  ARGB1555_unpack((ARGB1555_type)pal[src[0] >> 4], rgba + 0x4);
  ARGB1555_unpack((ARGB1555_type)pal[src[1] & 15], rgba + 0x8);
  ARGB1555_unpack((ARGB1555_type)pal[src[1] >> 4], rgba + 0xc);
}

static inline void ARGB1555_unpack_pal8(const uint8_t *src, const uint32_t *pal,
                                        uint8_t *rgba) {
  ARGB1555_unpack((ARGB1555_type)pal[src[0]], rgba + 0x0);
  ARGB1555_unpack((ARGB1555_type)pal[src[1]], rgba + 0x4);
  ARGB1555_unpack((ARGB1555_type)pal[src[2]], rgba + 0x8);
  ARGB1555_unpack((ARGB1555_type)pal[src[3]], rgba + 0xc);
}

/* RGB565 */
// typedef uint16_t RGB565_type;

static inline void RGB565_unpack(const RGB565_type src, uint8_t *rgba) {
  rgba[0] = COLOR_EXTEND_5((src & 0b1111100000000000) >> 8);
  rgba[1] = COLOR_EXTEND_6((src & 0b0000011111100000) >> 3);
  rgba[2] = COLOR_EXTEND_5((src & 0b0000000000011111) << 3);
  rgba[3] = 0xff;
}

static inline void RGB565_unpack_bitmap(const RGB565_type *src, uint8_t *rgba) {
  RGB565_unpack(src[0], rgba + 0x0);
  RGB565_unpack(src[1], rgba + 0x4);
  RGB565_unpack(src[2], rgba + 0x8);
  RGB565_unpack(src[3], rgba + 0xc);
}

static inline void RGB565_unpack_twiddled(const RGB565_type *src,
                                          uint8_t *rgba) {
  RGB565_unpack(src[0], rgba + 0x0);
  RGB565_unpack(src[1], rgba + 0x4);
  RGB565_unpack(src[2], rgba + 0x8);
  RGB565_unpack(src[3], rgba + 0xc);
}

static inline void RGB565_unpack_pal4(const uint8_t *src, const uint32_t *pal,
                                      uint8_t *rgba) {
  RGB565_unpack((RGB565_type)pal[src[0] & 15], rgba + 0x0);
  RGB565_unpack((RGB565_type)pal[src[0] >> 4], rgba + 0x4);
  RGB565_unpack((RGB565_type)pal[src[1] & 15], rgba + 0x8);
  RGB565_unpack((RGB565_type)pal[src[1] >> 4], rgba + 0xc);
}

static inline void RGB565_unpack_pal8(const uint8_t *src, const uint32_t *pal,
                                      uint8_t *rgba) {
  RGB565_unpack((RGB565_type)pal[src[0]], rgba + 0x0);
  RGB565_unpack((RGB565_type)pal[src[1]], rgba + 0x4);
  RGB565_unpack((RGB565_type)pal[src[2]], rgba + 0x8);
  RGB565_unpack((RGB565_type)pal[src[3]], rgba + 0xc);
}

/* UYVY422 */
// typedef uint16_t UYVY422_type;

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

static inline void UYVY422_unpack(UYVY422_type src0, UYVY422_type src1,
                                  uint8_t *rgba) {
  int u = (int)(src0 & 0xff) - 128;
  int y0 = (int)((src0 >> 8) & 0xff);
  int v = (int)((src1 & 0xff) & 0xff) - 128;
  int y1 = (int)((src1 >> 8) & 0xff);
  rgba[0] = yuv_to_r(y0, u, v);
  rgba[1] = yuv_to_g(y0, u, v);
  rgba[2] = yuv_to_b(y0, u, v);
  rgba[3] = 0xff;
  rgba[4] = yuv_to_r(y1, u, v);
  rgba[5] = yuv_to_g(y1, u, v);
  rgba[6] = yuv_to_b(y1, u, v);
  rgba[7] = 0xff;
}

static inline void UYVY422_unpack_bitmap(const UYVY422_type *src,
                                         uint8_t *rgba) {
  UYVY422_unpack(src[0], src[1], rgba + 0x0);
  UYVY422_unpack(src[2], src[3], rgba + 0x8);
}

static inline void UYVY422_unpack_twiddled(const UYVY422_type *src,
                                           uint8_t *rgba) {
  UYVY422_unpack(src[0], src[2], rgba + 0x0);
  UYVY422_unpack(src[1], src[3], rgba + 0x8);
}

/* ARGB4444 */
// typedef uint16_t ARGB4444_type;

static inline void ARGB4444_unpack(const ARGB4444_type src, uint8_t *rgba) {
  rgba[0] = COLOR_EXTEND_4((src & 0b0000111100000000) >> 4);
  rgba[1] = COLOR_EXTEND_4((src & 0b0000000011110000) << 0);
  rgba[2] = COLOR_EXTEND_4((src & 0b0000000000001111) << 4);
  rgba[3] = COLOR_EXTEND_4((src & 0b1111000000000000) >> 8);
}

static inline void ARGB4444_unpack_bitmap(const ARGB4444_type *src,
                                          uint8_t *rgba) {
  ARGB4444_unpack(src[0], rgba + 0x0);
  ARGB4444_unpack(src[1], rgba + 0x4);
  ARGB4444_unpack(src[2], rgba + 0x8);
  ARGB4444_unpack(src[3], rgba + 0xc);
}

static inline void ARGB4444_unpack_twiddled(const ARGB4444_type *src,
                                            uint8_t *rgba) {
  ARGB4444_unpack(src[0], rgba + 0x0);
  ARGB4444_unpack(src[1], rgba + 0x4);
  ARGB4444_unpack(src[2], rgba + 0x8);
  ARGB4444_unpack(src[3], rgba + 0xc);
}

static inline void ARGB4444_unpack_pal4(const uint8_t *src, const uint32_t *pal,
                                        uint8_t *rgba) {
  ARGB4444_unpack((ARGB4444_type)pal[src[0] & 15], rgba + 0x0);
  ARGB4444_unpack((ARGB4444_type)pal[src[0] >> 4], rgba + 0x4);
  ARGB4444_unpack((ARGB4444_type)pal[src[1] & 15], rgba + 0x8);
  ARGB4444_unpack((ARGB4444_type)pal[src[1] >> 4], rgba + 0xc);
}

static inline void ARGB4444_unpack_pal8(const uint8_t *src, const uint32_t *pal,
                                        uint8_t *rgba) {
  ARGB4444_unpack((ARGB4444_type)pal[src[0]], rgba + 0x0);
  ARGB4444_unpack((ARGB4444_type)pal[src[1]], rgba + 0x4);
  ARGB4444_unpack((ARGB4444_type)pal[src[2]], rgba + 0x8);
  ARGB4444_unpack((ARGB4444_type)pal[src[3]], rgba + 0xc);
}

/* ARGB8888 */
// typedef uint32_t ARGB8888_type;

static inline void ARGB8888_unpack(ARGB8888_type src, uint8_t *rgba) {
  rgba[0] = (src >> 16) & 0xff;
  rgba[1] = (src >> 8) & 0xff;
  rgba[2] = (src & 0xff);
  rgba[3] = (src >> 24) & 0xff;
}

static inline void ARGB8888_unpack_bitmap(const ARGB8888_type *src,
                                          uint8_t *rgba) {
  ARGB8888_unpack(src[0], rgba + 0x0);
  ARGB8888_unpack(src[1], rgba + 0x4);
  ARGB8888_unpack(src[2], rgba + 0x8);
  ARGB8888_unpack(src[3], rgba + 0xc);
}

static inline void ARGB8888_unpack_twiddled(const ARGB8888_type *src,
                                            uint8_t *rgba) {
  ARGB8888_unpack(src[0], rgba + 0x0);
  ARGB8888_unpack(src[1], rgba + 0x4);
  ARGB8888_unpack(src[2], rgba + 0x8);
  ARGB8888_unpack(src[3], rgba + 0xc);
}

static inline void ARGB8888_unpack_pal4(const uint8_t *src, const uint32_t *pal,
                                        uint8_t *rgba) {
  ARGB8888_unpack((ARGB8888_type)pal[src[0] & 15], rgba + 0x0);
  ARGB8888_unpack((ARGB8888_type)pal[src[0] >> 4], rgba + 0x4);
  ARGB8888_unpack((ARGB8888_type)pal[src[1] & 15], rgba + 0x8);
  ARGB8888_unpack((ARGB8888_type)pal[src[1] >> 4], rgba + 0xc);
}

static inline void ARGB8888_unpack_pal8(const uint8_t *src, const uint32_t *pal,
                                        uint8_t *rgba) {
  ARGB8888_unpack((ARGB8888_type)pal[src[0]], rgba + 0x0);
  ARGB8888_unpack((ARGB8888_type)pal[src[1]], rgba + 0x4);
  ARGB8888_unpack((ARGB8888_type)pal[src[2]], rgba + 0x8);
  ARGB8888_unpack((ARGB8888_type)pal[src[3]], rgba + 0xc);
}

/* RGBA */
// typedef uint32_t RGBA_type;

static inline void RGBA_pack(RGBA_type *dst, uint8_t *rgba) {
  *(uint32_t *)dst = *(uint32_t *)rgba;
}

static inline void RGBA_pack_bitmap(RGBA_type *dst, int x, int y, int stride,
                                    uint8_t *rgba) {
  RGBA_pack(&dst[y * stride + (x + 0)], rgba + 0x0);
  RGBA_pack(&dst[y * stride + (x + 1)], rgba + 0x4);
  RGBA_pack(&dst[y * stride + (x + 2)], rgba + 0x8);
  RGBA_pack(&dst[y * stride + (x + 3)], rgba + 0xc);
}

static inline void RGBA_pack_twiddled(RGBA_type *dst, int x, int y, int stride,
                                      uint8_t *rgba) {
  RGBA_pack(&dst[(y + 0) * stride + (x + 0)], rgba + 0x0);
  RGBA_pack(&dst[(y + 1) * stride + (x + 0)], rgba + 0x4);
  RGBA_pack(&dst[(y + 0) * stride + (x + 1)], rgba + 0x8);
  RGBA_pack(&dst[(y + 1) * stride + (x + 1)], rgba + 0xc);
}

/*
 * texture formats
 *
 * functions for converting from twiddled, compressed and paletted textures into
 * bitmaps to be registered with the render backend
 *
 * note, all pixel pack routines operate on 4 texels at a time, optimizing and
 * simplifying the logic for converting from twidddled and compressed textures
 * which both fundamentally work with 4 texels at a time. further, this allows
 * the UYVY422 unpacking routines (which work on 2 texels at a time) to not
 * need any additional special casing
 */

/* twiddled-format textures are stored in a reverse N order like:

   00 02 | 08 10
         |
   01 03 | 09 11
   -------------
   04 06 | 12 14
         |
   05 07 | 13 15

   a lookup table is generated to match an (x,y) pair to its twiddled index */
static int twitbl[1024];
static int twitbl_init = 0;

void pvr_init_twiddle_table() {
  if (twitbl_init) {
    return;
  }

  twitbl_init = 1;

  for (int i = 0; i < ARRAY_SIZE(twitbl); i++) {
    twitbl[i] = 0;

    for (int j = 0, k = 1; k <= i; j++, k <<= 1) {
      twitbl[i] |= (i & k) << j;
    }
  }
}

static int pvr_twiddle_pos(int x, int y) {
  return (twitbl[x] << 1) | twitbl[y];
}

#define define_convert_bitmap(FROM, TO)                                     \
  void convert_bitmap_##FROM##_##TO(const FROM##_type *src, TO##_type *dst, \
                                    int width, int height, int stride) {    \
    uint8_t rgba[4 * 4];                                                    \
                                                                            \
    for (int y = 0; y < height; y++) {                                      \
      for (int x = 0; x < width; x += 4) {                                  \
        FROM##_unpack_bitmap(&src[y * stride + x], rgba);                   \
        TO##_pack_bitmap(dst, x, y, width, rgba);                           \
      }                                                                     \
    }                                                                       \
  }

#define define_convert_twiddled(FROM, TO)                                     \
  void convert_twiddled_##FROM##_##TO(const FROM##_type *src, TO##_type *dst, \
                                      int width, int height) {                \
    pvr_init_twiddle_table();                                                 \
                                                                              \
    uint8_t rgba[4 * 4];                                                      \
    int size = MIN(width, height);                                            \
    int base = 0;                                                             \
                                                                              \
    for (int y = 0; y < height; y += size) {                                  \
      for (int x = 0; x < width; x += size) {                                 \
        for (int y2 = 0; y2 < size; y2 += 2) {                                \
          for (int x2 = 0; x2 < size; x2 += 2) {                              \
            int pos = base + pvr_twiddle_pos(x2, y2);                         \
            FROM##_unpack_twiddled(&src[pos], rgba);                          \
            TO##_pack_twiddled(dst, x + x2, y + y2, width, rgba);             \
          }                                                                   \
        }                                                                     \
        base += size * size;                                                  \
      }                                                                       \
    }                                                                         \
  }

#define define_convert_pal4(FROM, TO)                                 \
  void convert_pal4_##FROM##_##TO(const uint8_t *src, TO##_type *dst, \
                                  const uint32_t *palette, int width, \
                                  int height) {                       \
    pvr_init_twiddle_table();                                         \
                                                                      \
    uint8_t rgba[4 * 4];                                              \
    int size = MIN(width, height);                                    \
    int base = 0;                                                     \
                                                                      \
    for (int y = 0; y < height; y += size) {                          \
      for (int x = 0; x < width; x += size) {                         \
        for (int y2 = 0; y2 < size; y2 += 2) {                        \
          for (int x2 = 0; x2 < size; x2 += 2) {                      \
            int pos = base + pvr_twiddle_pos(x2, y2);                 \
            FROM##_unpack_pal4(&src[pos >> 1], palette, rgba);        \
            TO##_pack_twiddled(dst, x + x2, y + y2, width, rgba);     \
          }                                                           \
        }                                                             \
        base += size * size;                                          \
      }                                                               \
    }                                                                 \
  }

#define define_convert_pal8(FROM, TO)                                 \
  void convert_pal8_##FROM##_##TO(const uint8_t *src, TO##_type *dst, \
                                  const uint32_t *palette, int width, \
                                  int height) {                       \
    pvr_init_twiddle_table();                                         \
                                                                      \
    uint8_t rgba[4 * 4];                                              \
    int size = MIN(width, height);                                    \
    int base = 0;                                                     \
                                                                      \
    for (int y = 0; y < height; y += size) {                          \
      for (int x = 0; x < width; x += size) {                         \
        for (int y2 = 0; y2 < size; y2 += 2) {                        \
          for (int x2 = 0; x2 < size; x2 += 2) {                      \
            int pos = base + pvr_twiddle_pos(x2, y2);                 \
            FROM##_unpack_pal8(&src[pos], palette, rgba);             \
            TO##_pack_twiddled(dst, x + x2, y + y2, width, rgba);     \
          }                                                           \
        }                                                             \
        base += size * size;                                          \
      }                                                               \
    }                                                                 \
  }

#define define_convert_vq(FROM, TO)                                          \
  void convert_vq_##FROM##_##TO(const uint8_t *src, const uint8_t *codebook, \
                                TO##_type *dst, int width, int height) {     \
    pvr_init_twiddle_table();                                                \
                                                                             \
    uint8_t rgba[4 * 4];                                                     \
    int size = MIN(width, height);                                           \
    int base = 0;                                                            \
                                                                             \
    for (int y = 0; y < height; y += size) {                                 \
      for (int x = 0; x < width; x += size) {                                \
        for (int y2 = 0; y2 < size; y2 += 2) {                               \
          for (int x2 = 0; x2 < size; x2 += 2) {                             \
            int pos = base + pvr_twiddle_pos(x2, y2);                        \
            /* each codebook entry is 4x2 bytes long */                      \
            int idx = src[pos / 4] * 8;                                      \
            const FROM##_type *code = (const FROM##_type *)&codebook[idx];   \
            FROM##_unpack_twiddled(code, rgba);                              \
            TO##_pack_twiddled(dst, x + x2, y + y2, width, rgba);            \
          }                                                                  \
        }                                                                    \
        base += size * size;                                                 \
      }                                                                      \
    }                                                                        \
  }

define_convert_bitmap(ARGB1555, RGBA);
define_convert_bitmap(RGB565, RGBA);
define_convert_bitmap(UYVY422, RGBA);
define_convert_bitmap(ARGB4444, RGBA);

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
