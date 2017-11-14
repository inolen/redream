#include "guest/pvr/tex.h"
#include "core/core.h"
#include "render/render_backend.h"

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
typedef uint16_t ARGB1555_type;

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
typedef uint16_t RGB565_type;

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
typedef uint16_t UYVY422_type;

static inline uint8_t yuv_to_r(int y, int u, int v) {
  int r = y + (11 * v) / 8;
  return CLAMP(r, 0, 255);
}

static inline uint8_t yuv_to_g(int y, int u, int v) {
  int g = y - (11 * u + 22 * v) / 32;
  return CLAMP(g, 0, 255);
}

static inline uint8_t yuv_to_b(int y, int u, int v) {
  int b = y + (55 * u) / 32;
  return CLAMP(b, 0, 255);
}

static inline void UYVY422_unpack(UYVY422_type src0, UYVY422_type src1,
                                  uint8_t *a, uint8_t *b) {
  int u = (int)(src0 & 0xff) - 128;
  int y0 = (int)((src0 >> 8) & 0xff);
  int v = (int)((src1 & 0xff) & 0xff) - 128;
  int y1 = (int)((src1 >> 8) & 0xff);
  a[0] = yuv_to_r(y0, u, v);
  a[1] = yuv_to_g(y0, u, v);
  a[2] = yuv_to_b(y0, u, v);
  a[3] = 0xff;
  b[0] = yuv_to_r(y1, u, v);
  b[1] = yuv_to_g(y1, u, v);
  b[2] = yuv_to_b(y1, u, v);
  b[3] = 0xff;
}

static inline void UYVY422_unpack_bitmap(const UYVY422_type *src,
                                         uint8_t *rgba) {
  UYVY422_unpack(src[0], src[1], rgba + 0x0, rgba + 0x4);
  UYVY422_unpack(src[2], src[3], rgba + 0x8, rgba + 0xc);
}

static inline void UYVY422_unpack_twiddled(const UYVY422_type *src,
                                           uint8_t *rgba) {
  UYVY422_unpack(src[0], src[2], rgba + 0x0, rgba + 0x8);
  UYVY422_unpack(src[1], src[3], rgba + 0x4, rgba + 0xc);
}

/* ARGB4444 */
typedef uint16_t ARGB4444_type;

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
typedef uint32_t ARGB8888_type;

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
typedef uint32_t RGBA_type;

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
define_convert_vq(UYVY422, RGBA);

/*
 * texture loading
 */
static int compressed_mipmap_offsets[] = {
    0x00000, /* 1 x 1 */
    0x00001, /* 2 x 2 */
    0x00002, /* 4 x 4 */
    0x00006, /* 8 x 8 */
    0x00016, /* 16 x 16 */
    0x00056, /* 32 x 32 */
    0x00156, /* 64 x 64 */
    0x00556, /* 128 x 128 */
    0x01556, /* 256 x 256 */
    0x05556, /* 512 x 512 */
    0x15556, /* 1024 x 1024 */
};

static int paletted_4bpp_mipmap_offsets[] = {
    0x00003, /* 1 x 1 */
    0x00004, /* 2 x 2 */
    0x00008, /* 4 x 4 */
    0x0000c, /* 8 x 8 */
    0x0002c, /* 16 x 16 */
    0x000ac, /* 32 x 32 */
    0x002ac, /* 64 x 64 */
    0x00aac, /* 128 x 128 */
    0x02aac, /* 256 x 256 */
    0x0aaac, /* 512 x 512 */
    0x2aaac, /* 1024 x 1024 */
};

static int paletted_8bpp_mipmap_offsets[] = {
    0x00003, /* 1 x 1 */
    0x00004, /* 2 x 2 */
    0x00008, /* 4 x 4 */
    0x00018, /* 8 x 8 */
    0x00058, /* 16 x 16 */
    0x00158, /* 32 x 32 */
    0x00558, /* 64 x 64 */
    0x01558, /* 128 x 128 */
    0x05558, /* 256 x 256 */
    0x15558, /* 512 x 512 */
    0x55558, /* 1024 x 1024 */
};

static int nonpaletted_mipmap_offsets[] = {
    0x00006, /* 1 x 1 */
    0x00008, /* 2 x 2 */
    0x00010, /* 4 x 4 */
    0x00030, /* 8 x 8 */
    0x000b0, /* 16 x 16 */
    0x002b0, /* 32 x 32 */
    0x00ab0, /* 64 x 64 */
    0x02ab0, /* 128 x 128 */
    0x0aab0, /* 256 x 256 */
    0x2aab0, /* 512 x 512 */
    0xaaab0, /* 1024 x 1024 */
};

const struct pvr_tex_header *pvr_tex_header(const uint8_t *src) {
  /* skip the optional global index header, no idea what this means */
  uint32_t version;
  memcpy(&version, src, 4);

  if (memcmp((char *)&version, "GBIX", 4) == 0) {
    src += 4;

    uint32_t size;
    memcpy(&size, src, 4);
    src += 4;

    uint64_t index;
    CHECK_LE(size, sizeof(index));
    memcpy(&index, src, size);
    src += size;
  }

  /* skip the optional IMSZ header, again, no idea what this means */
  if (memcmp((char *)&version, "IMSZ", 4) == 0) {
    src += 4;

    /* no idea what this data is */
    src += 12;
  }

  /* validate header */
  const struct pvr_tex_header *header = (const struct pvr_tex_header *)src;
  if (memcmp((char *)&header->version, "PVRT", 4) != 0) {
    return NULL;
  }

  return header;
}

const uint8_t *pvr_tex_data(const uint8_t *src) {
  const struct pvr_tex_header *header = pvr_tex_header(src);
  const uint8_t *data = (const uint8_t *)(header + 1);
  int mipmaps = pvr_tex_mipmaps(header->texture_fmt);

  /* textures with mipmaps have an extra 4 bytes written at the end of the
     file. these extra 4 bytes appear to make the pvr loading code used by
     games generate texture addresses that are 4 bytes less than addresses
     of textures with mipmaps */
  if (mipmaps) {
    data -= 4;
  }

  return data;
}

void pvr_tex_decode(const uint8_t *src, int width, int height, int stride,
                    int texture_fmt, int pixel_fmt, const uint8_t *palette,
                    int palette_fmt, uint8_t *dst, int size) {
  int twiddled = pvr_tex_twiddled(texture_fmt);
  int compressed = pvr_tex_compressed(texture_fmt);
  int mipmaps = pvr_tex_mipmaps(texture_fmt);

  /* used by vq compressed textures */
  const uint8_t *codebook = src;
  const uint8_t *index = src + PVR_CODEBOOK_SIZE;

  /* mipmap textures contain data for 1 x 1 up to width x height. skip to the
     highest res and let the renderer backend generate its own mipmaps */
  if (mipmaps) {
    int u_size = ctz32(width);

    if (compressed) {
      /* for vq compressed textures the offset is only for the index data, the
         codebook is the same for all levels */
      index += compressed_mipmap_offsets[u_size];
    } else if (pixel_fmt == PVR_PXL_4BPP) {
      src += paletted_4bpp_mipmap_offsets[u_size];
    } else if (pixel_fmt == PVR_PXL_8BPP) {
      src += paletted_8bpp_mipmap_offsets[u_size];
    } else {
      src += nonpaletted_mipmap_offsets[u_size];
    }
  }

  /* aliases to cut down on copy and paste */
  const uint16_t *src16 = (const uint16_t *)src;
  const uint32_t *pal32 = (const uint32_t *)palette;
  uint32_t *dst32 = (uint32_t *)dst;

  switch (pixel_fmt) {
    case PVR_PXL_ARGB1555:
    case PVR_PXL_RESERVED:
      if (compressed) {
        convert_vq_ARGB1555_RGBA(index, codebook, dst32, width, height);
      } else if (twiddled) {
        convert_twiddled_ARGB1555_RGBA(src16, dst32, width, height);
      } else {
        convert_bitmap_ARGB1555_RGBA(src16, dst32, width, height, stride);
      }
      break;

    case PVR_PXL_RGB565:
      if (compressed) {
        convert_vq_RGB565_RGBA(index, codebook, dst32, width, height);
      } else if (twiddled) {
        convert_twiddled_RGB565_RGBA(src16, dst32, width, height);
      } else {
        convert_bitmap_RGB565_RGBA(src16, dst32, width, height, stride);
      }
      break;

    case PVR_PXL_ARGB4444:
      if (compressed) {
        convert_vq_ARGB4444_RGBA(index, codebook, dst32, width, height);
      } else if (twiddled) {
        convert_twiddled_ARGB4444_RGBA(src16, dst32, width, height);
      } else {
        convert_bitmap_ARGB4444_RGBA(src16, dst32, width, height, stride);
      }
      break;

    case PVR_PXL_YUV422:
      if (compressed) {
        convert_vq_UYVY422_RGBA(index, codebook, dst32, width, height);
      } else if (twiddled) {
        convert_twiddled_UYVY422_RGBA(src16, dst32, width, height);
      } else {
        convert_bitmap_UYVY422_RGBA(src16, dst32, width, height, stride);
      }
      break;

    case PVR_PXL_4BPP:
      CHECK(!compressed);
      switch (palette_fmt) {
        case PVR_PAL_ARGB1555:
          convert_pal4_ARGB1555_RGBA(src, dst32, pal32, width, height);
          break;

        case PVR_PAL_RGB565:
          convert_pal4_RGB565_RGBA(src, dst32, pal32, width, height);
          break;

        case PVR_PAL_ARGB4444:
          convert_pal4_ARGB4444_RGBA(src, dst32, pal32, width, height);
          break;

        case PVR_PAL_ARGB8888:
          convert_pal4_ARGB8888_RGBA(src, dst32, pal32, width, height);
          break;

        default:
          LOG_FATAL("pvr_tex_decode unsupported 4bpp palette format %d",
                    palette_fmt);
          break;
      }
      break;

    case PVR_PXL_8BPP:
      CHECK(!compressed);
      switch (palette_fmt) {
        case PVR_PAL_ARGB1555:
          convert_pal8_ARGB1555_RGBA(src, dst32, pal32, width, height);
          break;

        case PVR_PAL_RGB565:
          convert_pal8_RGB565_RGBA(src, dst32, pal32, width, height);
          break;

        case PVR_PAL_ARGB4444:
          convert_pal8_ARGB4444_RGBA(src, dst32, pal32, width, height);
          break;

        case PVR_PAL_ARGB8888:
          convert_pal8_ARGB8888_RGBA(src, dst32, pal32, width, height);
          break;

        default:
          LOG_FATAL("pvr_tex_decode unsupported 8bpp palette format %d",
                    palette_fmt);
          break;
      }
      break;

    default:
      LOG_FATAL("pvr_tex_decode unsupported pixel format %d", pixel_fmt);
      break;
  }
}
