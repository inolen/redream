#ifndef TA_H
#define TA_H

#include "guest/pvr/ta_types.h"
#include "guest/pvr/tex.h"

struct dreamcast;
struct ta;

struct ta *ta_create(struct dreamcast *dc);
void ta_destroy(struct ta *ta);

void ta_soft_reset(struct ta *ta);
void ta_start_render(struct ta *ta);
void ta_list_init(struct ta *ta);
void ta_list_cont(struct ta *ta);
void ta_yuv_init(struct ta *ta);
void ta_texture_info(struct ta *ta, union tsp tsp, union tcw tcw,
                     const uint8_t **texture, int *texture_size,
                     const uint8_t **palette, int *palette_size);

void ta_poly_write(struct ta *ta, uint32_t dst, const uint8_t *src, int size);
void ta_yuv_write(struct ta *ta, uint32_t dst, const uint8_t *src, int size);
void ta_texture_write(struct ta *ta, uint32_t dst, const uint8_t *src,
                      int size);

/*
 * parameter stream processing helpers, shared by both the ta and tr
 */
extern int ta_param_sizes[0x100 * TA_NUM_PARAMS * TA_NUM_VERTS];
extern int ta_poly_types[0x100 * TA_NUM_PARAMS * TA_NUM_LISTS];
extern int ta_vert_types[0x100 * TA_NUM_PARAMS * TA_NUM_LISTS];

static inline int ta_param_size(union pcw pcw, int vert_type) {
  return ta_param_sizes[pcw.obj_control * TA_NUM_PARAMS * TA_NUM_VERTS +
                        pcw.para_type * TA_NUM_VERTS + vert_type];
}

static inline int ta_poly_type(union pcw pcw) {
  return ta_poly_types[pcw.obj_control * TA_NUM_PARAMS * TA_NUM_LISTS +
                       pcw.para_type * TA_NUM_LISTS + pcw.list_type];
}

static inline int ta_vert_type(union pcw pcw) {
  return ta_vert_types[pcw.obj_control * TA_NUM_PARAMS * TA_NUM_LISTS +
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

void ta_init_tables();

/*
 * texture info helpers, shared by both the ta and tr
 */
uint32_t ta_texture_addr(union tsp tsp, union tcw tcw, int *size);
uint32_t ta_palette_addr(union tcw tcw, int *size);
int ta_texture_format(union tcw tcw);
int ta_texture_compressed(union tcw tcw);
int ta_texture_twiddled(union tcw tcw);
int ta_texture_mipmaps(union tcw tcw);
int ta_texture_width(union tsp tsp, union tcw tcw);
int ta_texture_height(union tsp tsp, union tcw tcw);
int ta_texture_stride(union tsp tsp, union tcw tcw, int stride);

#endif
