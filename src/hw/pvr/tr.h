#ifndef TR_H
#define TR_H

#include "core/rb_tree.h"
#include "hw/pvr/ta_types.h"
#include "video/render_backend.h"

struct tr;

typedef uint64_t texture_key_t;

struct texture_entry {
  union tsp tsp;
  union tcw tcw;

  /* source info */
  int frame;
  int dirty;
  const uint8_t *texture;
  int texture_size;
  const uint8_t *palette;
  int palette_size;

  /* backend info */
  enum pxl_format format;
  enum filter_mode filter;
  enum wrap_mode wrap_u;
  enum wrap_mode wrap_v;
  int width;
  int height;
  texture_handle_t handle;
};

/* provides abstraction around providing texture data to the renderer. when
   emulating the actual ta, textures will be provided from guest memory, but
   when playing back traces the textures will come from the trace itself */
struct texture_provider {
  void *data;
  struct texture_entry *(*find_texture)(void *, union tsp, union tcw);
};

/* debug structure which represents an individual tile parameter from a
   tile_context. used to scrub through a frame param by param in the
   tracer */
struct render_param {
  /* offset of parameter in input tile_context params */
  int offset;
  /* global list and vertex types at time of parsing */
  int list_type;
  int vertex_type;
  /* surf and vert in output render_context */
  struct surface *surf;
  struct vertex *vert;
};

/* represents a tile_context parsed into appropriate structures for the render
   backend */
struct render_context {
  /* input / output buffers supplied by caller */
  struct surface *surfs;
  int surfs_size;

  struct vertex *verts;
  int verts_size;

  int *sorted_surfs;
  int sorted_surfs_size;

  struct render_param *params;
  int params_size;

  /* output */
  float projection[16];
  int num_surfs;
  int num_verts;
  int num_params;
};

static inline texture_key_t tr_texture_key(union tsp tsp, union tcw tcw) {
  return ((uint64_t)tsp.full << 32) | tcw.full;
}

struct tr *tr_create(struct render_backend *rb,
                     struct texture_provider *provider);
void tr_destroy(struct tr *tr);

void tr_parse_context(struct tr *tr, const struct tile_ctx *ctx,
                      struct render_context *rc);
void tr_render_context(struct tr *tr, const struct render_context *rc);

#endif
