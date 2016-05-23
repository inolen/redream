#ifndef TILE_ACCELERATOR_H
#define TILE_ACCELERATOR_H

#include "hw/holly/ta_types.h"
#include "hw/memory.h"

#ifdef __cplusplus
extern "C" {
#endif

struct dreamcast_s;
struct rb_s;

AM_DECLARE(ta_fifo_map);

int ta_get_param_size(pcw_t pcw, int vertex_type);
int ta_get_poly_type(pcw_t pcw);
int ta_get_vert_type(pcw_t pcw);

struct ta_s *ta_create(struct dreamcast_s *dc, struct rb_s *rb);
void ta_destroy(struct ta_s *ta);

#ifdef __cplusplus
}
#endif

#endif
