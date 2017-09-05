#ifndef TA_H
#define TA_H

#include "core/profiler.h"
#include "guest/memory.h"
#include "guest/pvr/ta_types.h"

struct dreamcast;
struct ta;
struct tr_provider;

DECLARE_COUNTER(ta_renders);

AM_DECLARE(ta_fifo_map);

#define TA_CODEBOOK_SIZE (256 * 8)

extern int g_param_sizes[0x100 * TA_NUM_PARAMS * TA_NUM_VERTS];
extern int g_poly_types[0x100 * TA_NUM_PARAMS * TA_NUM_LISTS];
extern int g_vertex_types[0x100 * TA_NUM_PARAMS * TA_NUM_LISTS];

static inline int ta_get_param_size(union pcw pcw, int vertex_type) {
  return g_param_sizes[pcw.obj_control * TA_NUM_PARAMS * TA_NUM_VERTS +
                       pcw.para_type * TA_NUM_VERTS + vertex_type];
}

static inline int ta_get_poly_type(union pcw pcw) {
  return g_poly_types[pcw.obj_control * TA_NUM_PARAMS * TA_NUM_LISTS +
                      pcw.para_type * TA_NUM_LISTS + pcw.list_type];
}

static inline int ta_get_vert_type(union pcw pcw) {
  return g_vertex_types[pcw.obj_control * TA_NUM_PARAMS * TA_NUM_LISTS +
                        pcw.para_type * TA_NUM_LISTS + pcw.list_type];
}

static inline int ta_pcw_list_type_valid(union pcw pcw, int current_list_type) {
  /* pcw.list_type is only valid for the first global parameter / object list
     set after TA_LIST_INIT or a previous TA_PARAM_END_OF_LIST */
  return current_list_type == TA_NUM_LISTS &&
         (pcw.para_type == TA_PARAM_OBJ_LIST_SET ||
          pcw.para_type == TA_PARAM_POLY_OR_VOL ||
          pcw.para_type == TA_PARAM_SPRITE);
}

static inline uint32_t ta_texture_addr(union tcw tcw) {
  return tcw.texture_addr << 3;
}

static inline int ta_texture_twiddled(union tcw tcw) {
  return !tcw.scan_order ||
         /* paletted textures are always twiddled */
         tcw.pixel_fmt == TA_PXL_8BPP || tcw.pixel_fmt == TA_PXL_4BPP;
}

static inline int ta_texture_compressed(union tcw tcw) {
  return tcw.vq_compressed;
}

static inline int ta_texture_mipmaps(union tcw tcw) {
  return ta_texture_twiddled(tcw) && tcw.mip_mapped;
}

static inline int ta_texture_width(union tsp tsp, union tcw tcw) {
  return 8 << tsp.texture_u_size;
}

static inline int ta_texture_height(union tsp tsp, union tcw tcw) {
  int mipmaps = ta_texture_mipmaps(tcw);
  int height = 8 << tsp.texture_v_size;
  if (mipmaps) {
    height = ta_texture_width(tsp, tcw);
  }
  return height;
}

static inline int ta_texture_bpp(union tcw tcw) {
  int bpp = 16;
  if (tcw.pixel_fmt == TA_PXL_8BPP) {
    bpp = 8;
  } else if (tcw.pixel_fmt == TA_PXL_4BPP) {
    bpp = 4;
  }
  return bpp;
}

static inline int ta_texture_size(union tsp tsp, union tcw tcw) {
  int compressed = ta_texture_compressed(tcw);
  int mipmaps = ta_texture_mipmaps(tcw);
  int width = ta_texture_width(tsp, tcw);
  int height = ta_texture_height(tsp, tcw);
  int bpp = ta_texture_bpp(tcw);
  int texture_size = 0;
  if (compressed) {
    texture_size += TA_CODEBOOK_SIZE;
  }
  int min_width = mipmaps ? 1 : width;
  for (int i = width; i >= min_width; i /= 2) {
    texture_size += (width * height * bpp) >> 3;
  }
  return texture_size;
}

void ta_init_tables();

struct ta *ta_create(struct dreamcast *dc);
void ta_destroy(struct ta *ta);

void ta_texture_info(struct ta *ta, union tsp tsp, union tcw tcw,
                     const uint8_t **texture, int *texture_size,
                     const uint8_t **palette, int *palette_size);

#endif
