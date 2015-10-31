#ifndef PIXEL_CONVERT_H
#define PIXEL_CONVERT_H

#include <algorithm>

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

namespace dreavm {
namespace hw {
namespace holly {

class ARGB1555 {
 public:
  typedef uint16_t data_type;

  static inline void Read(data_type px, uint8_t *r, uint8_t *g, uint8_t *b,
                          uint8_t *a) {
    *a = (px >> 15) & 0x1;
    *r = (px >> 10) & 0x1f;
    *g = (px >> 5) & 0x1f;
    *b = px & 0x1f;
  }

  static inline void Write(data_type *dst, uint8_t r, uint8_t g, uint8_t b,
                           uint8_t a) {
    *dst =
        ((a & 0x1) << 15) | ((r & 0x1f) << 10) | ((g & 0x1f) << 5) | (b & 0x1f);
  }
};

class RGBA5551 {
 public:
  typedef uint16_t data_type;

  static inline void Read(data_type px, uint8_t *r, uint8_t *g, uint8_t *b,
                          uint8_t *a) {
    *r = (px >> 11) & 0x1f;
    *g = (px >> 6) & 0x1f;
    *b = (px >> 1) & 0x1f;
    *a = px & 0x1;
  }

  static inline void Write(data_type *dst, uint8_t r, uint8_t g, uint8_t b,
                           uint8_t a) {
    *dst =
        ((r & 0x1f) << 11) | ((g & 0x1f) << 6) | ((b & 0x1f) << 1) | (a & 0x1);
  }
};

class RGB565 {
 public:
  typedef uint16_t data_type;

  static inline void Read(data_type px, uint8_t *r, uint8_t *g, uint8_t *b,
                          uint8_t *a) {
    *r = (px >> 11) & 0x1f;
    *g = (px >> 5) & 0x3f;
    *b = px & 0x1f;
    *a = 0xff;
  }

  static inline void Write(data_type *dst, uint8_t r, uint8_t g, uint8_t b,
                           uint8_t a) {
    *dst = ((r & 0x1f) << 11) | ((g & 0x3f) << 5) | (b & 0x1f);
  }
};

class ARGB4444 {
 public:
  typedef uint16_t data_type;

  static inline void Read(data_type px, uint8_t *r, uint8_t *g, uint8_t *b,
                          uint8_t *a) {
    *a = (px >> 12) & 0xf;
    *r = (px >> 8) & 0xf;
    *g = (px >> 4) & 0xf;
    *b = px & 0xf;
  }

  static inline void Write(data_type *dst, uint8_t r, uint8_t g, uint8_t b,
                           uint8_t a) {
    *dst = ((a & 0xf) << 12) | ((r & 0xf) << 8) | ((g & 0xf) << 4) | (b & 0xf);
  }
};

class RGBA4444 {
 public:
  typedef uint16_t data_type;

  static inline void Read(data_type px, uint8_t *r, uint8_t *g, uint8_t *b,
                          uint8_t *a) {
    *r = (px >> 12) & 0xf;
    *g = (px >> 8) & 0xf;
    *b = (px >> 4) & 0xf;
    *a = px & 0xf;
  }

  static inline void Write(data_type *dst, uint8_t r, uint8_t g, uint8_t b,
                           uint8_t a) {
    *dst = ((r & 0xf) << 12) | ((g & 0xf) << 8) | ((b & 0xf) << 4) | (a & 0xf);
  }
};

class ARGB8888 {
 public:
  typedef uint32_t data_type;

  static inline void Read(data_type px, uint8_t *r, uint8_t *g, uint8_t *b,
                          uint8_t *a) {
    *a = (px >> 24) & 0xff;
    *r = (px >> 16) & 0xff;
    *g = (px >> 8) & 0xff;
    *b = px & 0xff;
  }

  static inline void Write(data_type *dst, uint8_t r, uint8_t g, uint8_t b,
                           uint8_t a) {
    *dst = (a << 24) | (r << 16) | (g << 8) | b;
  }
};

class RGBA8888 {
 public:
  typedef uint32_t data_type;

  static inline void Read(data_type px, uint8_t *r, uint8_t *g, uint8_t *b,
                          uint8_t *a) {
    *r = (px >> 24) & 0xff;
    *g = (px >> 16) & 0xff;
    *b = (px >> 8) & 0xff;
    *a = px & 0xff;
  }

  static inline void Write(data_type *dst, uint8_t r, uint8_t g, uint8_t b,
                           uint8_t a) {
    *dst = (r << 24) | (g << 16) | (b << 8) | a;
  }
};

class PixelConvert {
 public:
  template <typename FROM, typename TO>
  static void Convert(const typename FROM::data_type *src,
                      typename TO::data_type *dst, int width, int height) {
    uint8_t r, g, b, a;

    for (int y = 0; y < height; ++y) {
      for (int x = 0; x < width; ++x) {
        FROM::Read(*(src++), &r, &g, &b, &a);
        TO::Write(dst++, r, g, b, a);
      }
    }
  }

  template <typename FROM, typename TO>
  static void ConvertTwiddled(const typename FROM::data_type *src,
                              typename TO::data_type *dst, int width,
                              int height) {
    int min = std::min(width, height);
    uint8_t r, g, b, a;

    for (int y = 0; y < height; ++y) {
      for (int x = 0; x < width; ++x) {
        int tidx = TWIDIDX(x, y, min);
        FROM::Read(src[tidx], &r, &g, &b, &a);
        TO::Write(dst++, r, g, b, a);
      }
    }
  }

  template <typename FROM, typename TO>
  static void ConvertPal4(const uint8_t *src, typename TO::data_type *dst,
                          const uint32_t *palette, int width, int height) {
    int min = std::min(width, height);
    uint8_t r, g, b, a;

    // always twiddled
    for (int y = 0; y < height; ++y) {
      for (int x = 0; x < width; ++x) {
        int tidx = TWIDIDX(x, y, min);
        int palette_idx = src[tidx >> 1];
        if (tidx & 1) {
          palette_idx >>= 4;
        } else {
          palette_idx &= 0xf;
        }
        typename FROM::data_type entry =
            *(reinterpret_cast<const typename FROM::data_type *>(
                &palette[palette_idx]));
        FROM::Read(entry, &r, &g, &b, &a);
        TO::Write(dst++, r, g, b, a);
      }
    }
  }

  template <typename FROM, typename TO>
  static void ConvertPal8(const uint8_t *src, typename TO::data_type *dst,
                          const uint32_t *palette, int width, int height) {
    int min = std::min(width, height);
    uint8_t r, g, b, a;

    // always twiddled
    for (int y = 0; y < height; ++y) {
      for (int x = 0; x < width; ++x) {
        int palette_idx = src[TWIDIDX(x, y, min)];
        typename FROM::data_type entry =
            *reinterpret_cast<const typename FROM::data_type *>(
                &palette[palette_idx]);
        FROM::Read(entry, &r, &g, &b, &a);
        TO::Write(dst++, r, g, b, a);
      }
    }
  }

  template <typename FROM, typename TO>
  static void ConvertVQ(const uint8_t *codebook, const uint8_t *index,
                        typename TO::data_type *dst, int width, int height) {
    int min = std::min(width, height);
    uint8_t r, g, b, a;

    // always twiddled
    for (int y = 0; y < height; ++y) {
      for (int x = 0; x < width; ++x) {
        int tidx = TWIDIDX(x, y, min);
        auto code = reinterpret_cast<const typename FROM::data_type *>(
            &codebook[index[tidx / 4] * 8 + ((tidx % 4) * 2)]);
        FROM::Read(*code, &r, &g, &b, &a);
        TO::Write(dst++, r, g, b, a);
      }
    }
  }
};
}
}
}

#endif
