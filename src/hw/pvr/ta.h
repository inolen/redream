#ifndef TILE_ACCELERATOR_H
#define TILE_ACCELERATOR_H

#include "hw/memory.h"
#include "hw/pvr/ta_types.h"

struct dreamcast;
struct texture_provider;

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

struct ta;

void ta_build_tables();

AM_DECLARE(ta_fifo_map);

struct ta *ta_create(struct dreamcast *dc);
void ta_destroy(struct ta *ta);

struct texture_provider *ta_texture_provider(struct ta *ta);
int ta_lock_pending_context(struct ta *ta, struct tile_ctx **pending_ctx,
                            int *pending_frame);
void ta_unlock_pending_context(struct ta *ta);

#endif
