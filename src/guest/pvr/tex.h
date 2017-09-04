#ifndef TEX_H
#define TEX_H

#include <stdint.h>

typedef uint16_t ARGB1555_type;
typedef uint16_t RGB565_type;
typedef uint16_t UYVY422_type;
typedef uint16_t ARGB4444_type;
typedef uint32_t ARGB8888_type;
typedef uint32_t RGBA_type;

#define declare_convert_bitmap(FROM, TO)                                    \
  void convert_bitmap_##FROM##_##TO(const FROM##_type *src, TO##_type *dst, \
                                    int width, int height, int stride);

#define declare_convert_twiddled(FROM, TO)                                    \
  void convert_twiddled_##FROM##_##TO(const FROM##_type *src, TO##_type *dst, \
                                      int width, int height);

#define declare_convert_pal4(FROM, TO)                                \
  void convert_pal4_##FROM##_##TO(const uint8_t *src, TO##_type *dst, \
                                  const uint32_t *palette, int width, \
                                  int height);

#define declare_convert_pal8(FROM, TO)                                \
  void convert_pal8_##FROM##_##TO(const uint8_t *src, TO##_type *dst, \
                                  const uint32_t *palette, int width, \
                                  int height);

#define declare_convert_vq(FROM, TO)                                         \
  void convert_vq_##FROM##_##TO(const uint8_t *src, const uint8_t *codebook, \
                                TO##_type *dst, int width, int height);

declare_convert_bitmap(ARGB1555, RGBA);
declare_convert_bitmap(RGB565, RGBA);
declare_convert_bitmap(UYVY422, RGBA);
declare_convert_bitmap(ARGB4444, RGBA);

declare_convert_twiddled(ARGB1555, RGBA);
declare_convert_twiddled(RGB565, RGBA);
declare_convert_twiddled(UYVY422, RGBA);
declare_convert_twiddled(ARGB4444, RGBA);

declare_convert_pal4(ARGB1555, RGBA);
declare_convert_pal4(RGB565, RGBA);
declare_convert_pal4(ARGB4444, RGBA);
declare_convert_pal4(ARGB8888, RGBA);

declare_convert_pal8(ARGB1555, RGBA);
declare_convert_pal8(RGB565, RGBA);
declare_convert_pal8(ARGB4444, RGBA);
declare_convert_pal8(ARGB8888, RGBA);

declare_convert_vq(ARGB1555, RGBA);
declare_convert_vq(RGB565, RGBA);
declare_convert_vq(ARGB4444, RGBA);

#endif
