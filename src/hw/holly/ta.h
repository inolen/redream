#ifndef TILE_ACCELERATOR_H
#define TILE_ACCELERATOR_H

#include "hw/holly/ta_types.h"
#include "hw/memory.h"

struct dreamcast_s;
struct rb_s;

extern int g_param_sizes[0x100 * TA_NUM_PARAMS * TA_NUM_VERT_TYPES];
extern int g_poly_types[0x100 * TA_NUM_PARAMS * TA_NUM_LISTS];
extern int g_vertex_types[0x100 * TA_NUM_PARAMS * TA_NUM_LISTS];

static inline int ta_get_param_size(pcw_t pcw, int vertex_type) {
  return g_param_sizes[pcw.obj_control * TA_NUM_PARAMS * TA_NUM_VERT_TYPES +
                       pcw.para_type * TA_NUM_VERT_TYPES + vertex_type];
}

static inline int ta_get_poly_type(pcw_t pcw) {
  return g_poly_types[pcw.obj_control * TA_NUM_PARAMS * TA_NUM_LISTS +
                      pcw.para_type * TA_NUM_LISTS + pcw.list_type];
}

static inline int ta_get_vert_type(pcw_t pcw) {
  return g_vertex_types[pcw.obj_control * TA_NUM_PARAMS * TA_NUM_LISTS +
                        pcw.para_type * TA_NUM_LISTS + pcw.list_type];
}

void ta_build_tables();

struct ta_s *ta_create(struct dreamcast_s *dc, struct rb_s *rb);
void ta_destroy(struct ta_s *ta);

AM_DECLARE(ta_fifo_map);

#ifdef __cplusplus
}
#endif

#endif
