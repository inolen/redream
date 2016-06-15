#ifndef TR_H
#define TR_H

#include "hw/holly/ta_types.h"
#include "renderer/backend.h"

#ifdef __cplusplus
extern "C" {
#endif

struct tr_s;

// register_texture_cb / get_texture_cb provide an abstraction around
// providing textures to the renderer. when emulating the actual TA,
// textures will be provided from guest memory, but when playing
// back traces the textures will come from the trace itself
typedef uint64_t texture_key_t;

typedef struct {
  // texture registration input
  const tile_ctx_t *ctx;
  tsp_t tsp;
  tcw_t tcw;
  const uint8_t *palette;
  const uint8_t *data;

  // texture registration output. normally, the handle is the only information
  // needed, but the rest is used by the tracer for debugging purposes
  texture_handle_t handle;
  pxl_format_t format;
  filter_mode_t filter;
  wrap_mode_t wrap_u;
  wrap_mode_t wrap_v;
  bool mipmaps;
  int width;
  int height;
} texture_reg_t;

typedef void (*register_texture_cb)(void *, texture_reg_t *reg);

typedef texture_handle_t (*get_texture_cb)(void *, const tile_ctx_t *, tsp_t,
                                           tcw_t, void *, register_texture_cb);

// represents the parse state after each ta parameter. used to visually scrub
// through the scene parameter by parameter in the tracer
typedef struct {
  int num_surfs;
  int num_verts;
} param_state_t;

// tile context parsed into appropriate structures for the render backend
typedef struct {
  // supplied by caller
  surface_t *surfs;
  int surfs_size;

  vertex_t *verts;
  int verts_size;

  int *sorted_surfs;
  int sorted_surfs_size;

  param_state_t *states;
  int states_size;

  //
  float projection[16];
  int num_surfs;
  int num_verts;
  int num_states;
} render_ctx_t;

texture_key_t tr_get_texture_key(tsp_t tsp, tcw_t tcw);

void tr_parse_context(struct tr_s *tr, const tile_ctx_t *ctx,
                      render_ctx_t *rctx);
void tr_render_context(struct tr_s *tr, const render_ctx_t *rctx);

struct tr_s *tr_create(struct rb_s *rb, void *get_tex_data,
                       get_texture_cb get_tex);
void tr_destroy(struct tr_s *tr);

#ifdef __cplusplus
}
#endif

#endif
