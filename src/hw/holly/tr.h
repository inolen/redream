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
// back traces the textures will come from the trace itself.
typedef uint64_t texture_key_t;

typedef struct {
  texture_handle_t handle;
  pxl_format_t format;
  filter_mode_t filter;
  wrap_mode_t wrap_u;
  wrap_mode_t wrap_v;
  bool mipmaps;
  int width;
  int height;
} registered_texture_t;

typedef registered_texture_t (*register_texture_cb)(void *, const ta_ctx_t *,
                                                    tsp_t, tcw_t,
                                                    const uint8_t *,
                                                    const uint8_t *);

typedef texture_handle_t (*get_texture_cb)(void *, const ta_ctx_t *, tsp_t,
                                           tcw_t, void *, register_texture_cb);

// parsed render context
typedef struct {
  float projection[16];

  surface_t *surfs;
  int surfs_size;
  int surfs_capacity;

  vertex_t *verts;
  int verts_size;
  int verts_capacity;

  int *sorted_surfs;
  int sorted_surfs_size;
  int sorted_surfs_capacity;

  // // map tile context offset -> number of surfs / verts rendered
  // // for tracing
  // struct ParamMapEntry {
  //   int num_surfs;
  //   int num_verts;
  // };
  // std::map<int, ParamMapEntry> param_map;
} tr_ctx_t;

texture_key_t tr_get_texture_key(tsp_t tsp, tcw_t tcw);

struct tr_s *tr_create(struct rb_s *rb, void *get_tex_data,
                       get_texture_cb get_tex);
void tr_destroy(struct tr_s *tr);

void tr_parse_context(struct tr_s *tr, const ta_ctx_t *ctx, tr_ctx_t *rctx,
                      bool map_params);
void tr_clear_context(tr_ctx_t *rctx);
void tr_render_ctx(struct tr_s *tr, const tr_ctx_t *rctx);

#ifdef __cplusplus
}
#endif

#endif
