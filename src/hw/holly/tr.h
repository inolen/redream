#ifndef TR_H
#define TR_H

#include "core/rb_tree.h"
#include "hw/holly/ta_types.h"
#include "renderer/backend.h"

struct tr;

typedef uint64_t texture_key_t;

struct texture_entry {
  union tsp tsp;
  union tcw tcw;

  // source info
  int frame;
  int dirty;
  const uint8_t *texture;
  int texture_size;
  const uint8_t *palette;
  int palette_size;

  // backend info
  enum pxl_format format;
  enum filter_mode filter;
  enum wrap_mode wrap_u;
  enum wrap_mode wrap_v;
  bool mipmaps;
  int width;
  int height;
  texture_handle_t handle;
};

// provides abstraction around providing texture data to the renderer. when
// emulating the actual ta, textures will be provided from guest memory, but
// when playing back traces the textures will come from the trace itself
struct texture_provider {
  void *data;
  struct texture_entry *(*find_texture)(void *, union tsp, union tcw);
};

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

static inline texture_key_t tr_texture_key(union tsp tsp, union tcw tcw) {
  return ((uint64_t)tsp.full << 32) | tcw.full;
}

void tr_parse_context(struct tr *tr, const struct tile_ctx *ctx, int frame,
                      struct render_ctx *rctx);
void tr_render_context(struct tr *tr, const struct render_ctx *rctx);

struct tr *tr_create(struct rb *rb, struct texture_provider *provider);
void tr_destroy(struct tr *tr);

#endif
