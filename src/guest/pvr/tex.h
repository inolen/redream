#ifndef TEX_H
#define TEX_H

#include <stdint.h>

#define PVR_CODEBOOK_SIZE (256 * 8)

enum pvr_texture_fmt {
  PVR_TEX_INVALID = 0x0,
  PVR_TEX_TWIDDLED = 0x1,
  PVR_TEX_TWIDDLED_MIPMAPS = 0x2,
  PVR_TEX_VQ = 0x3,
  PVR_TEX_VQ_MIPMAPS = 0x4,
  PVR_TEX_PALETTE_4BPP = 0x5,
  PVR_TEX_PALETTE_4BPP_MIPMAPS = 0x6,
  PVR_TEX_PALETTE_8BPP = 0x7,
  PVR_TEX_PALETTE_8BPP_MIPMAPS = 0x8,
  PVR_TEX_BITMAP_RECT = 0x9,
  PVR_TEX_BITMAP = 0xb,
  PVR_TEX_TWIDDLED_RECT = 0xd,
};

enum pvr_pixel_fmt {
  PVR_PXL_ARGB1555,
  PVR_PXL_RGB565,
  PVR_PXL_ARGB4444,
  PVR_PXL_YUV422,
  PVR_PXL_BUMPMAP,
  PVR_PXL_4BPP,
  PVR_PXL_8BPP,
  PVR_PXL_RESERVED, /* treated as ARGB1555 */
};

enum pvr_palette_fmt {
  PVR_PAL_ARGB1555,
  PVR_PAL_RGB565,
  PVR_PAL_ARGB4444,
  PVR_PAL_ARGB8888,
};

#pragma pack(push, 1)
struct pvr_tex_header {
  uint32_t version;
  uint32_t size;
  uint8_t pixel_fmt;
  uint8_t texture_fmt;
  uint16_t padding;
  uint16_t width;
  uint16_t height;
};
#pragma pack(pop)

static inline int pvr_tex_twiddled(int texture_fmt) {
  return texture_fmt == PVR_TEX_TWIDDLED ||
         texture_fmt == PVR_TEX_TWIDDLED_MIPMAPS ||
         texture_fmt == PVR_TEX_PALETTE_4BPP ||
         texture_fmt == PVR_TEX_PALETTE_4BPP_MIPMAPS ||
         texture_fmt == PVR_TEX_PALETTE_8BPP ||
         texture_fmt == PVR_TEX_PALETTE_8BPP_MIPMAPS ||
         texture_fmt == PVR_TEX_TWIDDLED_RECT;
}

static inline int pvr_tex_compressed(int texture_fmt) {
  return texture_fmt == PVR_TEX_VQ || texture_fmt == PVR_TEX_VQ_MIPMAPS;
}

static inline int pvr_tex_mipmaps(int texture_fmt) {
  return texture_fmt == PVR_TEX_TWIDDLED_MIPMAPS ||
         texture_fmt == PVR_TEX_VQ_MIPMAPS ||
         texture_fmt == PVR_TEX_PALETTE_4BPP_MIPMAPS ||
         texture_fmt == PVR_TEX_PALETTE_8BPP_MIPMAPS;
}

const struct pvr_tex_header *pvr_tex_header(const uint8_t *src);
const uint8_t *pvr_tex_data(const uint8_t *src);

void pvr_tex_decode(const uint8_t *data, int width, int height, int stride,
                    int texture_fmt, int pixel_fmt, const uint8_t *palette,
                    int pal_pixel_fmt, uint8_t *out, int size);

#endif
