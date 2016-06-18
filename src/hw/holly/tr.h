#ifndef TR_H
#define TR_H

#include "hw/holly/ta_types.h"
#include "renderer/backend.h"

struct tr;

// register_texture_cb / get_texture_cb provide an abstraction around
// providing textures to the renderer. when emulating the actual TA,
// textures will be provided from guest memory, but when playing
// back traces the textures will come from the trace itself
typedef uint64_t texture_key_t;

struct texture_reg {
  // texture registration input
  const struct tile_ctx *ctx;
  union tsp tsp;
  union tcw tcw;
  const uint8_t *palette;
  const uint8_t *texture;

  // texture registration output. normally, the handle is the only information
  // needed, but the rest is used by the tracer for debugging purposes
  texture_handle_t handle;
  enum pxl_format format;
  enum filter_mode filter;
  enum wrap_mode wrap_u;
  enum wrap_mode wrap_v;
  bool mipmaps;
  int width;
  int height;
};

typedef void (*register_texture_cb)(void *, struct texture_reg *reg);

typedef texture_handle_t (*get_texture_cb)(void *, const struct tile_ctx *,
                                           union tsp, union tcw, void *,
                                           register_texture_cb);

// represents the parse state after each ta parameter. used to visually scrub
// through the scene parameter by parameter in the tracer
struct param_state {
  int num_surfs;
  int num_verts;
};

// tile context parsed into appropriate structures for the render backend
struct render_ctx {
  // supplied by caller
  struct surface *surfs;
  int surfs_size;

  struct vertex *verts;
  int verts_size;

  int *sorted_surfs;
  int sorted_surfs_size;

  struct param_state *states;
  int states_size;

  //
  float projection[16];
  int num_surfs;
  int num_verts;
  int num_states;
};

texture_key_t tr_get_texture_key(union tsp tsp, union tcw tcw);

void tr_parse_context(struct tr *tr, const struct tile_ctx *ctx,
                      struct render_ctx *rctx);
void tr_render_context(struct tr *tr, const struct render_ctx *rctx);

struct tr *tr_create(struct rb *rb, void *get_tex_data, get_texture_cb get_tex);
void tr_destroy(struct tr *tr);

#endif
