#ifndef TR_H
#define TR_H

/* tile renderer code. responsible for parsing a raw tile_context into
   draw commands to be passed to the supplied render backend */

#include "core/rb_tree.h"
#include "hw/pvr/ta_types.h"
#include "render/render_backend.h"

struct tr;

typedef uint64_t tr_texture_key_t;

struct tr_texture {
  union tsp tsp;
  union tcw tcw;
  unsigned frame;
  int dirty;

  /* source info */
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
struct tr_provider {
  void *userdata;
  void (*clear_textures)(void *);
  struct tr_texture *(*find_texture)(void *, union tsp, union tcw);
};

struct tr_param {
  /* offset of parameter in tile_context param stream */
  int offset;
  /* global list and vertex types at time of parsing */
  int list_type;
  int vertex_type;
  /* last surf / vert generated for the param */
  int last_surf;
  int last_vert;
};

struct tr_list {
  int surfs[TA_MAX_SURFS];
  int num_surfs;
};

struct tr_context {
  /* transforms incoming windows space coordinates to ndc space */
  float minz, maxz;
  float projection[16];

  /* parsed surfaces and vertices, ready to be passed to the render backend */
  struct ta_surface surfs[TA_MAX_SURFS];
  int num_surfs;

  struct ta_vertex verts[TA_MAX_VERTS];
  int num_verts;

  /* sorted list of surfaces corresponding to each of the ta's polygon lists */
  struct tr_list lists[TA_NUM_LISTS];

  /* debug structures for stepping through the param stream in the tracer */
  struct tr_param params[TA_MAX_PARAMS];
  int num_params;
};

static inline tr_texture_key_t tr_texture_key(union tsp tsp, union tcw tcw) {
  return ((uint64_t)tsp.full << 32) | tcw.full;
}

struct tr *tr_create(struct render_backend *rb, struct tr_provider *provider);
void tr_destroy(struct tr *tr);

void tr_parse_context(struct tr *tr, const struct tile_context *ctx,
                      struct tr_context *rc);
void tr_render_context(struct tr *tr, const struct tr_context *rc);

#endif
