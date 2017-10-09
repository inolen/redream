#include <stdio.h>
#include "core/core.h"
#include "core/filesystem.h"
#include "guest/pvr/tex.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

char *texture_fmt_names[] = {
    NULL,
    "TWIDDLED",
    "TWIDDLED_MIPMAPS",
    "VQ",
    "VQ_MIPMAPS",
    "PALETTE_4BPP",
    NULL,
    "PALETTE_8BPP",
    NULL,
    "PLANAR_RECT",
    NULL,
    "PLANAR",
    NULL,
    "TWIDDLED_RECT",
};

char *pixel_fmt_names[] = {
    "ARGB1555", "RGB565", "ARGB4444", "YUV422",
};

uint8_t *read_tex(const char *filename) {
  FILE *fp = fopen(filename, "rb");
  if (!fp) {
    LOG_WARNING("failed to open '%s'", filename);
    return 0;
  }

  fseek(fp, 0, SEEK_END);
  int size = ftell(fp);
  fseek(fp, 0, SEEK_SET);

  uint8_t *buffer = malloc(size);
  int n = (int)fread(buffer, 1, size, fp);
  CHECK_EQ(n, size);
  fclose(fp);

  return buffer;
}

void convert_tex(const char *texname) {
  LOG_INFO("#==--------------------------------------------------==#");
  LOG_INFO("# %s", texname);
  LOG_INFO("#==--------------------------------------------------==#");

  uint8_t *buffer = read_tex(texname);

  const struct pvr_tex_header *header = pvr_tex_header(buffer);

  if (!header) {
    LOG_WARNING("convert_tex failed to find a valid PVRT header");
    return;
  }

  const uint8_t *data = pvr_tex_data(buffer);

  CHECK_LT(header->pixel_fmt, ARRAY_SIZE(pixel_fmt_names));

  /* dump header */
  LOG_INFO("version:      %.4s", (char *)&header->version);
  LOG_INFO("size:         %d bytes", header->size);
  LOG_INFO("pixel_fmt:    %s", pixel_fmt_names[header->pixel_fmt]);
  LOG_INFO("texture_fmt:  %s", texture_fmt_names[header->texture_fmt]);
  LOG_INFO("width:        %d", header->width);
  LOG_INFO("height:       %d", header->height);
  LOG_INFO("");

  /* convert each mip level to png */
  static uint8_t converted[1024 * 1024 * 4];
  int mipmaps = pvr_tex_mipmaps(header->texture_fmt);
  int levels = mipmaps ? ctz32(header->width) + 1 : 1;

  while (levels--) {
    int mip_width = header->width >> levels;
    int mip_height = header->height >> levels;
    pvr_tex_decode(data, mip_width, mip_height, mip_width, header->texture_fmt,
                   header->pixel_fmt, NULL, 0, converted, sizeof(converted));

    char pngname[PATH_MAX];
    snprintf(pngname, sizeof(pngname), "%s.%dx%d.png", texname, mip_width,
             mip_height);

    LOG_INFO("writing %s", pngname);

    int stride = mip_width * 4;
    int res =
        stbi_write_png(pngname, mip_width, mip_height, 4, converted, stride);
    CHECK_NE(res, 0);
  }

  free(buffer);
}

int main(int argc, char **argv) {
  for (int i = 1; i < argc; i++) {
    convert_tex(argv[i]);
  }

  return EXIT_SUCCESS;
}
