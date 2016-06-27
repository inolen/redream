#ifndef TILE_ACCELERATOR_H
#define TILE_ACCELERATOR_H

#include "hw/holly/ta_types.h"
#include "hw/memory.h"

struct dreamcast;
struct rb;

extern int g_param_sizes[0x100 * TA_NUM_PARAMS * TA_NUM_VERT_TYPES];
extern int g_poly_types[0x100 * TA_NUM_PARAMS * TA_NUM_LISTS];
extern int g_vertex_types[0x100 * TA_NUM_PARAMS * TA_NUM_LISTS];

static inline int ta_get_param_size(union pcw pcw, int vertex_type) {
  return g_param_sizes[pcw.obj_control * TA_NUM_PARAMS * TA_NUM_VERT_TYPES +
                       pcw.para_type * TA_NUM_VERT_TYPES + vertex_type];
}

static inline int ta_get_poly_type(union pcw pcw) {
  return g_poly_types[pcw.obj_control * TA_NUM_PARAMS * TA_NUM_LISTS +
                      pcw.para_type * TA_NUM_LISTS + pcw.list_type];
}

static inline int ta_get_vert_type(union pcw pcw) {
  return g_vertex_types[pcw.obj_control * TA_NUM_PARAMS * TA_NUM_LISTS +
                        pcw.para_type * TA_NUM_LISTS + pcw.list_type];
}

struct ta;

void ta_build_tables();

struct ta *ta_create(struct dreamcast *dc, struct rb *rb);
void ta_destroy(struct ta *ta);

AM_DECLARE(ta_fifo_map);

#endif
