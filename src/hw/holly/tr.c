#include <float.h>
#include <string.h>
#include "core/assert.h"
#include "core/core.h"
#include "core/profiler.h"
#include "hw/holly/pixel_convert.h"
#include "hw/holly/ta.h"
#include "hw/holly/tr.h"

struct tr {
  struct rb *rb;

  void *get_texture_data;
  get_texture_cb get_texture;

  // current global state
  const union poly_param *last_poly;
  const union vert_param *last_vertex;
  int list_type;
  int vertex_type;
  float face_color[4];
  float face_offset_color[4];
  int last_sorted_surf;
};

static int compressed_mipmap_offsets[] = {
    0x00006,  // 8 x 8
    0x00016,  // 16 x 16
    0x00056,  // 32 x 32
    0x00156,  // 64 x 64
    0x00556,  // 128 x 128
    0x01556,  // 256 x 256
    0x05556,  // 512 x 512
    0x15556,  // 1024 x 1024
};

static int paletted_4bpp_mipmap_offsets[] = {
    0x0000c,  // 8 x 8
    0x0002c,  // 16 x 16
    0x000ac,  // 32 x 32
    0x002ac,  // 64 x 64
    0x00aac,  // 128 x 128
    0x02aac,  // 256 x 256
    0x0aaac,  // 512 x 512
    0x2aaac,  // 1024 x 1024
};

static int paletted_8bpp_mipmap_offsets[] = {
    0x00018,  // 8 x 8
    0x00058,  // 16 x 16
    0x00158,  // 32 x 32
    0x00558,  // 64 x 64
    0x01558,  // 128 x 128
    0x05558,  // 256 x 256
    0x15558,  // 512 x 512
    0x55558,  // 1024 x 1024
};

static int nonpaletted_mipmap_offsets[] = {
    0x00030,  // 8 x 8
    0x000b0,  // 16 x 16
    0x002b0,  // 32 x 32
    0x00ab0,  // 64 x 64
    0x02ab0,  // 128 x 128
    0x0aab0,  // 256 x 256
    0x2aab0,  // 512 x 512
    0xaaab0,  // 1024 x 1024
};

static inline enum depth_func translate_depth_func(uint32_t depth_func) {
  static enum depth_func depth_funcs[] = {
      DEPTH_NEVER, DEPTH_GREATER, DEPTH_EQUAL,  DEPTH_GEQUAL,
      DEPTH_LESS,  DEPTH_NEQUAL,  DEPTH_LEQUAL, DEPTH_ALWAYS};
  return depth_funcs[depth_func];
}

static inline enum cull_face translate_cull(uint32_t cull_mode) {
  static enum cull_face cull_modes[] = {CULL_NONE, CULL_NONE, CULL_FRONT,
                                        CULL_BACK};
  return cull_modes[cull_mode];
}

static inline enum blend_func translate_src_blend_func(uint32_t blend_func) {
  static enum blend_func src_blend_funcs[] = {
      BLEND_ZERO,      BLEND_ONE,
      BLEND_DST_COLOR, BLEND_ONE_MINUS_DST_COLOR,
      BLEND_SRC_ALPHA, BLEND_ONE_MINUS_SRC_ALPHA,
      BLEND_DST_ALPHA, BLEND_ONE_MINUS_DST_ALPHA};
  return src_blend_funcs[blend_func];
}

static inline enum blend_func translate_dst_blend_func(uint32_t blend_func) {
  static enum blend_func dst_blend_funcs[] = {
      BLEND_ZERO,      BLEND_ONE,
      BLEND_SRC_COLOR, BLEND_ONE_MINUS_SRC_COLOR,
      BLEND_SRC_ALPHA, BLEND_ONE_MINUS_SRC_ALPHA,
      BLEND_DST_ALPHA, BLEND_ONE_MINUS_DST_ALPHA};
  return dst_blend_funcs[blend_func];
}

static inline enum shade_mode translate_shade_mode(uint32_t shade_mode) {
  static enum shade_mode shade_modes[] = {
      SHADE_DECAL, SHADE_MODULATE, SHADE_DECAL_ALPHA, SHADE_MODULATE_ALPHA};
  return shade_modes[shade_mode];
}

static inline uint32_t abgr_to_rgba(uint32_t v) {
  return (v & 0xff000000) | ((v & 0xff) << 16) | (v & 0xff00) |
         ((v & 0xff0000) >> 16);
}

static inline uint8_t float_to_u8(float x) {
  return MIN(MAX((uint32_t)(x * 255.0f), 0u), 255u);
}

static inline uint32_t float_to_rgba(float r, float g, float b, float a) {
  return (float_to_u8(a) << 24) | (float_to_u8(b) << 16) |
         (float_to_u8(g) << 8) | float_to_u8(r);
}

static void tr_register_texture(void *data, struct texture_reg *reg) {
  static uint8_t converted[1024 * 1024 * 4];

  struct tr *tr = data;
  const struct tile_ctx *ctx = reg->ctx;
  union tsp tsp = reg->tsp;
  union tcw tcw = reg->tcw;
  const uint8_t *palette = reg->palette;
  const uint8_t *texture = reg->texture;
  const uint8_t *input = texture;
  const uint8_t *output = texture;

  // textures are either twiddled and vq compressed, twiddled and uncompressed
  // or planar
  bool twiddled = !tcw.scan_order;
  bool compressed = tcw.vq_compressed;
  bool mip_mapped = !tcw.scan_order && tcw.mip_mapped;

  // get texture dimensions
  int width = 8 << tsp.texture_u_size;
  int height = mip_mapped ? width : 8 << tsp.texture_v_size;
  int stride = width;
  if (!twiddled && tcw.stride_select) {
    stride = ctx->stride;
  }

  // FIXME used for texcoords, not width / height of texture
  // if (planar && tcw.stride_select) {
  //   width = ctx->stride << 5;
  // }

  // mipmap textures contain data for 1 x 1 up to width x height. skip to the
  // highest res texture and let the renderer backend generate its own mipmaps
  if (mip_mapped) {
    if (compressed) {
      // for vq compressed textures the offset is only for the index data, the
      // codebook is the same for all levels
      input += compressed_mipmap_offsets[tsp.texture_u_size];
    } else if (tcw.pixel_format == TA_PIXEL_4BPP) {
      input += paletted_4bpp_mipmap_offsets[tsp.texture_u_size];
    } else if (tcw.pixel_format == TA_PIXEL_8BPP) {
      input += paletted_8bpp_mipmap_offsets[tsp.texture_u_size];
    } else {
      input += nonpaletted_mipmap_offsets[tsp.texture_u_size];
    }
  }

  // used by vq compressed textures
  static const int codebook_size = 256 * 8;
  const uint8_t *codebook = texture;
  const uint8_t *index = input + codebook_size;

  enum pxl_format pixel_fmt = PXL_INVALID;
  switch (tcw.pixel_format) {
    case TA_PIXEL_1555:
    case TA_PIXEL_RESERVED:
      output = converted;
      pixel_fmt = PXL_RGBA5551;
      if (compressed) {
        convert_vq_ARGB1555_RGBA5551(codebook, index, (uint16_t *)converted,
                                     width, height);
      } else if (twiddled) {
        convert_twiddled_ARGB1555_RGBA5551(
            (const uint16_t *)input, (uint16_t *)converted, width, height);
      } else {
        convert_ARGB1555_RGBA5551((const uint16_t *)input,
                                  (uint16_t *)converted, stride, height);
      }
      break;

    case TA_PIXEL_565:
      output = converted;
      pixel_fmt = PXL_RGB565;
      if (compressed) {
        convert_vq_RGB565_RGB565(codebook, index, (uint16_t *)converted, width,
                                 height);
      } else if (twiddled) {
        convert_twiddled_RGB565_RGB565((const uint16_t *)input,
                                       (uint16_t *)converted, width, height);
      } else {
        convert_RGB565_RGB565((const uint16_t *)input, (uint16_t *)converted,
                              stride, height);
      }
      break;

    case TA_PIXEL_4444:
      output = converted;
      pixel_fmt = PXL_RGBA4444;
      if (compressed) {
        convert_vq_ARGB4444_RGBA4444(codebook, index, (uint16_t *)converted,
                                     width, height);
      } else if (twiddled) {
        convert_twiddled_ARGB4444_RGBA4444(
            (const uint16_t *)input, (uint16_t *)converted, width, height);
      } else {
        convert_ARGB4444_RGBA4444((const uint16_t *)input,
                                  (uint16_t *)converted, stride, height);
      }
      break;

    case TA_PIXEL_4BPP:
      CHECK(!compressed);
      output = converted;
      switch (ctx->pal_pxl_format) {
        case TA_PAL_ARGB4444:
          pixel_fmt = PXL_RGBA4444;
          convert_pal4_ARGB4444_RGBA4444(input, (uint16_t *)converted,
                                         (const uint32_t *)palette, width,
                                         height);
          break;

        default:
          LOG_FATAL("Unsupported 4bpp palette pixel format %d",
                    ctx->pal_pxl_format);
          break;
      }
      break;

    case TA_PIXEL_8BPP:
      CHECK(!compressed);
      output = converted;
      switch (ctx->pal_pxl_format) {
        case TA_PAL_ARGB4444:
          pixel_fmt = PXL_RGBA4444;
          convert_pal8_ARGB4444_RGBA4444(input, (uint16_t *)converted,
                                         (const uint32_t *)palette, width,
                                         height);
          break;

        case TA_PAL_ARGB8888:
          pixel_fmt = PXL_RGBA8888;
          convert_pal8_ARGB8888_RGBA8888(input, (uint32_t *)converted,
                                         (const uint32_t *)palette, width,
                                         height);
          break;

        default:
          LOG_FATAL("Unsupported 8bpp palette pixel format %d",
                    ctx->pal_pxl_format);
          break;
      }
      break;

    default:
      LOG_FATAL("Unsupported tcw pixel format %d", tcw.pixel_format);
      break;
  }

  // ignore trilinear filtering for now
  enum filter_mode filter =
      tsp.filter_mode == 0 ? FILTER_NEAREST : FILTER_BILINEAR;
  enum wrap_mode wrap_u =
      tsp.clamp_u ? WRAP_CLAMP_TO_EDGE
                  : (tsp.flip_u ? WRAP_MIRRORED_REPEAT : WRAP_REPEAT);
  enum wrap_mode wrap_v =
      tsp.clamp_v ? WRAP_CLAMP_TO_EDGE
                  : (tsp.flip_v ? WRAP_MIRRORED_REPEAT : WRAP_REPEAT);

  texture_handle_t handle =
      rb_register_texture(tr->rb, pixel_fmt, filter, wrap_u, wrap_v, mip_mapped,
                          width, height, output);

  //
  reg->handle = handle;
  reg->format = pixel_fmt;
  reg->filter = filter;
  reg->wrap_u = wrap_u;
  reg->wrap_v = wrap_v;
  reg->mipmaps = mip_mapped;
  reg->width = width;
  reg->height = height;
}

static struct surface *tr_alloc_surf(struct tr *tr, struct render_ctx *rctx,
                                     bool copy_from_prev) {
  // either reset the surface state, or copy the state from the previous surface
  CHECK_LT(rctx->num_surfs, rctx->surfs_size);
  int id = rctx->num_surfs++;
  struct surface *surf = &rctx->surfs[id];

  if (copy_from_prev) {
    *surf = rctx->surfs[id - 1];
  } else {
    memset(surf, 0, sizeof(*surf));
  }

  // star verts at the end
  surf->first_vert = rctx->num_verts;
  surf->num_verts = 0;

  // default sort the surface
  rctx->sorted_surfs[id] = id;

  return surf;
}

static struct vertex *tr_alloc_vert(struct tr *tr, struct render_ctx *rctx) {
  CHECK_LT(rctx->num_verts, rctx->verts_size);
  int id = rctx->num_verts++;
  struct vertex *v = &rctx->verts[id];
  memset(v, 0, sizeof(*v));

  // update vertex count on the current surface
  struct surface *surf = &rctx->surfs[rctx->num_surfs - 1];
  surf->num_verts++;

  return v;
}

static void tr_discard_incomplete_surf(struct tr *tr, struct render_ctx *rctx) {
  // free up the last surface if it wasn't finished
  if (tr->last_vertex && !tr->last_vertex->type0.pcw.end_of_strip) {
    rctx->num_surfs--;
  }
}

// FIXME we could offload a lot of this to the GPU, generating shaders
// for different combinations of ISP/union tsp parameters once the logic is
// ironed out
// FIXME honor use alpha
// FIXME honor ignore tex alpha
static void tr_parse_color(struct tr *tr, uint32_t base_color,
                           uint32_t *color) {
  *color = abgr_to_rgba(base_color);

  // if (!tr->last_poly->type0.tsp.use_alpha) {
  //   color[3] = 1.0f;
  // }
}

static void tr_parse_color_intensity(struct tr *tr, float base_intensity,
                                     uint32_t *color) {
  *color = float_to_rgba(tr->face_color[0] * base_intensity,
                         tr->face_color[1] * base_intensity,
                         tr->face_color[2] * base_intensity, tr->face_color[3]);

  // if (!tr->last_poly->type0.tsp.use_alpha) {
  //   color[3] = 1.0f;
  // }
}

static void tr_parse_color_rgba(struct tr *tr, float r, float g, float b,
                                float a, uint32_t *color) {
  *color = float_to_rgba(r, g, b, a);

  // if (!tr->last_poly->type0.tsp.use_alpha) {
  //   color[3] = 1.0f;
  // }
}

static void tr_parse_offset_color(struct tr *tr, uint32_t offset_color,
                                  uint32_t *color) {
  if (!tr->last_poly->type0.isp_tsp.offset) {
    *color = 0;
  } else {
    *color = abgr_to_rgba(offset_color);
  }
}

static void tr_parse_offset_color_rgba(struct tr *tr, float r, float g, float b,
                                       float a, uint32_t *color) {
  if (!tr->last_poly->type0.isp_tsp.offset) {
    *color = 0;
  } else {
    *color = float_to_rgba(r, g, b, a);
  }
}

static void tr_parse_offset_color_intensity(struct tr *tr,
                                            float offset_intensity,
                                            uint32_t *color) {
  if (!tr->last_poly->type0.isp_tsp.offset) {
    *color = 0;
  } else {
    *color = float_to_rgba(tr->face_offset_color[0] * offset_intensity,
                           tr->face_offset_color[1] * offset_intensity,
                           tr->face_offset_color[2] * offset_intensity,
                           tr->face_offset_color[3]);
  }
}

static int tr_parse_bg_vert(const struct tile_ctx *ctx, int offset,
                            struct vertex *v) {
  v->xyz[0] = *(float *)&ctx->bg_vertices[offset];
  v->xyz[1] = *(float *)&ctx->bg_vertices[offset + 4];
  v->xyz[2] = *(float *)&ctx->bg_vertices[offset + 8];
  offset += 12;

  if (ctx->bg_isp.texture) {
    LOG_FATAL("Unsupported bg_isp.texture");
    // v->uv[0] = *(float *)(&ctx->bg_vertices[offset]);
    // v->uv[1] = *(float *)(&ctx->bg_vertices[offset + 4]);
    // offset += 8;
  }

  uint32_t base_color = *(uint32_t *)&ctx->bg_vertices[offset];
  v->color = abgr_to_rgba(base_color);
  offset += 4;

  if (ctx->bg_isp.offset) {
    LOG_FATAL("Unsupported bg_isp.offset");
    // uint32_t offset_color = *(uint32_t *)(&ctx->bg_vertices[offset]);
    // v->offset_color[0] = ((offset_color >> 16) & 0xff) / 255.0f;
    // v->offset_color[1] = ((offset_color >> 16) & 0xff) / 255.0f;
    // v->offset_color[2] = ((offset_color >> 16) & 0xff) / 255.0f;
    // v->offset_color[3] = 0.0f;
    // offset += 4;
  }

  return offset;
}

static void tr_parse_bg(struct tr *tr, const struct tile_ctx *ctx,
                        struct render_ctx *rctx) {
  // translate the surface
  struct surface *surf = tr_alloc_surf(tr, rctx, false);

  surf->texture = 0;
  surf->depth_write = !ctx->bg_isp.z_write_disable;
  surf->depth_func = translate_depth_func(ctx->bg_isp.depth_compare_mode);
  surf->cull = translate_cull(ctx->bg_isp.culling_mode);
  surf->src_blend = BLEND_NONE;
  surf->dst_blend = BLEND_NONE;

  // translate the first 3 vertices
  struct vertex *v0 = tr_alloc_vert(tr, rctx);
  struct vertex *v1 = tr_alloc_vert(tr, rctx);
  struct vertex *v2 = tr_alloc_vert(tr, rctx);
  struct vertex *v3 = tr_alloc_vert(tr, rctx);

  int offset = 0;
  offset = tr_parse_bg_vert(ctx, offset, v0);
  offset = tr_parse_bg_vert(ctx, offset, v1);
  offset = tr_parse_bg_vert(ctx, offset, v2);

  // override the xyz values supplied by ISP_BACKGND_T. while the hardware docs
  // act like the should be correct, they're most definitely not in most cases
  v0->xyz[0] = 0.0f;
  v0->xyz[1] = (float)ctx->video_height;
  v0->xyz[2] = ctx->bg_depth;
  v1->xyz[0] = 0.0f;
  v1->xyz[1] = 0.0f;
  v1->xyz[2] = ctx->bg_depth;
  v2->xyz[0] = (float)ctx->video_width;
  v2->xyz[1] = (float)ctx->video_height;
  v2->xyz[2] = ctx->bg_depth;

  // 4th vertex isn't supplied, fill it out automatically
  v3->xyz[0] = v2->xyz[0];
  v3->xyz[1] = v1->xyz[1];
  v3->xyz[2] = ctx->bg_depth;
  v3->color = v0->color;
  v3->offset_color = v0->offset_color;
  v3->uv[0] = v2->uv[0];
  v3->uv[1] = v1->uv[1];
}

// NOTE this offset color implementation is not correct at all, see the
// Texture/Shading Instruction in the union tsp instruction word
static void tr_parse_poly_param(struct tr *tr, const struct tile_ctx *ctx,
                                struct render_ctx *rctx, const uint8_t *data) {
  tr_discard_incomplete_surf(tr, rctx);

  const union poly_param *param = (const union poly_param *)data;

  tr->last_poly = param;
  tr->last_vertex = NULL;
  tr->list_type = param->type0.pcw.list_type;
  tr->vertex_type = ta_get_vert_type(param->type0.pcw);

  int poly_type = ta_get_poly_type(param->type0.pcw);
  switch (poly_type) {
    case 0: {
      // uint32_t sdma_data_size;
      // uint32_t sdma_next_addr;
    } break;

    case 1: {
      tr->face_color[0] = param->type1.face_color_r;
      tr->face_color[1] = param->type1.face_color_g;
      tr->face_color[2] = param->type1.face_color_b;
      tr->face_color[3] = param->type1.face_color_a;
    } break;

    case 2: {
      tr->face_color[0] = param->type2.face_color_r;
      tr->face_color[1] = param->type2.face_color_g;
      tr->face_color[2] = param->type2.face_color_b;
      tr->face_color[3] = param->type2.face_color_a;
      tr->face_offset_color[0] = param->type2.face_offset_color_r;
      tr->face_offset_color[1] = param->type2.face_offset_color_g;
      tr->face_offset_color[2] = param->type2.face_offset_color_b;
      tr->face_offset_color[3] = param->type2.face_offset_color_a;
    } break;

    case 5: {
      tr->face_color[0] = ((param->sprite.base_color >> 16) & 0xff) / 255.0f;
      tr->face_color[1] = ((param->sprite.base_color >> 8) & 0xff) / 255.0f;
      tr->face_color[2] = (param->sprite.base_color & 0xff) / 255.0f;
      tr->face_color[3] = ((param->sprite.base_color >> 24) & 0xff) / 255.0f;
      tr->face_offset_color[0] =
          ((param->sprite.offset_color >> 16) & 0xff) / 255.0f;
      tr->face_offset_color[1] =
          ((param->sprite.offset_color >> 8) & 0xff) / 255.0f;
      tr->face_offset_color[2] = (param->sprite.offset_color & 0xff) / 255.0f;
      tr->face_offset_color[3] =
          ((param->sprite.offset_color >> 24) & 0xff) / 255.0f;
    } break;

    case 6:
      // don't do anything with modifier volume yet
      return;

    default:
      LOG_FATAL("Unsupported poly type %d", poly_type);
      break;
  }

  // setup the new surface
  struct surface *surf = tr_alloc_surf(tr, rctx, false);
  surf->depth_write = !param->type0.isp_tsp.z_write_disable;
  surf->depth_func =
      translate_depth_func(param->type0.isp_tsp.depth_compare_mode);
  surf->cull = translate_cull(param->type0.isp_tsp.culling_mode);
  surf->src_blend = translate_src_blend_func(param->type0.tsp.src_alpha_instr);
  surf->dst_blend = translate_dst_blend_func(param->type0.tsp.dst_alpha_instr);
  surf->shade = translate_shade_mode(param->type0.tsp.texture_shading_instr);
  surf->ignore_tex_alpha = param->type0.tsp.ignore_tex_alpha;

  // override a few surface parameters based on the list type
  if (tr->list_type != TA_LIST_TRANSLUCENT &&
      tr->list_type != TA_LIST_TRANSLUCENT_MODVOL) {
    surf->src_blend = BLEND_NONE;
    surf->dst_blend = BLEND_NONE;
  } else if ((tr->list_type == TA_LIST_TRANSLUCENT ||
              tr->list_type == TA_LIST_TRANSLUCENT_MODVOL) &&
             ctx->autosort) {
    surf->depth_func = DEPTH_LEQUAL;
  } else if (tr->list_type == TA_LIST_PUNCH_THROUGH) {
    surf->depth_func = DEPTH_GEQUAL;
  }

  if (param->type0.pcw.texture) {
    surf->texture = tr->get_texture(tr->get_texture_data, ctx, param->type0.tsp,
                                    param->type0.tcw, tr, &tr_register_texture);
  } else {
    surf->texture = 0;
  }
}

static void tr_parse_spritea_vert(struct tr *tr, const union vert_param *param,
                                  int i, struct vertex *vert) {
  // FIXME this is assuming all sprites are billboards
  // z isn't specified for i == 3
  vert->xyz[0] = param->sprite0.xyz[i][0];
  vert->xyz[1] = param->sprite0.xyz[i][1];
  vert->xyz[2] = param->sprite0.xyz[0][2];

  tr_parse_color_rgba(tr, tr->face_color[0], tr->face_color[1],
                      tr->face_color[2], tr->face_color[3], &vert->color);
  tr_parse_offset_color_rgba(tr, tr->face_offset_color[0],
                             tr->face_offset_color[1], tr->face_offset_color[2],
                             tr->face_offset_color[3], &vert->offset_color);
};

static void tr_parse_spriteb_vert(struct tr *tr, const union vert_param *param,
                                  int i, struct vertex *vert) {
  // FIXME this is assuming all sprites are billboards
  // z isn't specified for i == 3
  vert->xyz[0] = param->sprite1.xyz[i][0];
  vert->xyz[1] = param->sprite1.xyz[i][1];
  vert->xyz[2] = param->sprite1.xyz[0][2];
  tr_parse_color_rgba(tr, tr->face_color[0], tr->face_color[1],
                      tr->face_color[2], tr->face_color[3], &vert->color);
  tr_parse_offset_color_rgba(tr, tr->face_offset_color[0],
                             tr->face_offset_color[1], tr->face_offset_color[2],
                             tr->face_offset_color[3], &vert->offset_color);
  uint32_t u, v;
  if (i == 3) {
    u = (param->sprite1.uv[0] & 0xffff0000);
    v = (param->sprite1.uv[2] & 0x0000ffff) << 16;
  } else {
    u = (param->sprite1.uv[i] & 0xffff0000);
    v = (param->sprite1.uv[i] & 0x0000ffff) << 16;
  }
  vert->uv[0] = *(float *)&u;
  vert->uv[1] = *(float *)&v;
}

static void tr_parse_vert_param(struct tr *tr, const struct tile_ctx *ctx,
                                struct render_ctx *rctx, const uint8_t *data) {
  const union vert_param *param = (const union vert_param *)data;
  // If there is no need to change the Global Parameters, a struct vertex
  // Parameter
  // for
  // the next polygon may be input immediately after inputting a vertex_t
  // Parameter for which "End of Strip" was specified.
  if (tr->last_vertex && tr->last_vertex->type0.pcw.end_of_strip) {
    tr_alloc_surf(tr, rctx, true);
  }
  tr->last_vertex = param;

  switch (tr->vertex_type) {
    case 0: {
      struct vertex *vert = tr_alloc_vert(tr, rctx);
      vert->xyz[0] = param->type0.xyz[0];
      vert->xyz[1] = param->type0.xyz[1];
      vert->xyz[2] = param->type0.xyz[2];
      tr_parse_color(tr, param->type0.base_color, &vert->color);
      vert->offset_color = 0;
      vert->uv[0] = 0.0f;
      vert->uv[1] = 0.0f;
    } break;

    case 1: {
      struct vertex *vert = tr_alloc_vert(tr, rctx);
      vert->xyz[0] = param->type1.xyz[0];
      vert->xyz[1] = param->type1.xyz[1];
      vert->xyz[2] = param->type1.xyz[2];
      tr_parse_color_rgba(tr, param->type1.base_color_r,
                          param->type1.base_color_g, param->type1.base_color_b,
                          param->type1.base_color_a, &vert->color);
      vert->offset_color = 0;
      vert->uv[0] = 0.0f;
      vert->uv[1] = 0.0f;
    } break;

    case 2: {
      struct vertex *vert = tr_alloc_vert(tr, rctx);
      vert->xyz[0] = param->type2.xyz[0];
      vert->xyz[1] = param->type2.xyz[1];
      vert->xyz[2] = param->type2.xyz[2];
      tr_parse_color_intensity(tr, param->type2.base_intensity, &vert->color);
      vert->offset_color = 0;
      vert->uv[0] = 0.0f;
      vert->uv[1] = 0.0f;
    } break;

    case 3: {
      struct vertex *vert = tr_alloc_vert(tr, rctx);
      vert->xyz[0] = param->type3.xyz[0];
      vert->xyz[1] = param->type3.xyz[1];
      vert->xyz[2] = param->type3.xyz[2];
      tr_parse_color(tr, param->type3.base_color, &vert->color);
      tr_parse_offset_color(tr, param->type3.offset_color, &vert->offset_color);
      vert->uv[0] = param->type3.uv[0];
      vert->uv[1] = param->type3.uv[1];
    } break;

    case 4: {
      struct vertex *vert = tr_alloc_vert(tr, rctx);
      vert->xyz[0] = param->type4.xyz[0];
      vert->xyz[1] = param->type4.xyz[1];
      vert->xyz[2] = param->type4.xyz[2];
      tr_parse_color(tr, param->type4.base_color, &vert->color);
      tr_parse_offset_color(tr, param->type4.offset_color, &vert->offset_color);
      uint32_t u = param->type4.uv[0] << 16;
      uint32_t v = param->type4.uv[0] << 16;
      vert->uv[0] = *(float *)&u;
      vert->uv[1] = *(float *)&v;
    } break;

    case 5: {
      struct vertex *vert = tr_alloc_vert(tr, rctx);
      vert->xyz[0] = param->type5.xyz[0];
      vert->xyz[1] = param->type5.xyz[1];
      vert->xyz[2] = param->type5.xyz[2];
      tr_parse_color_rgba(tr, param->type5.base_color_r,
                          param->type5.base_color_g, param->type5.base_color_b,
                          param->type5.base_color_a, &vert->color);
      tr_parse_offset_color_rgba(
          tr, param->type5.offset_color_r, param->type5.offset_color_g,
          param->type5.offset_color_b, param->type5.offset_color_a,
          &vert->offset_color);
      vert->uv[0] = param->type5.uv[0];
      vert->uv[1] = param->type5.uv[1];
    } break;

    case 6: {
      struct vertex *vert = tr_alloc_vert(tr, rctx);
      vert->xyz[0] = param->type6.xyz[0];
      vert->xyz[1] = param->type6.xyz[1];
      vert->xyz[2] = param->type6.xyz[2];
      tr_parse_color_rgba(tr, param->type6.base_color_r,
                          param->type6.base_color_g, param->type6.base_color_b,
                          param->type6.base_color_a, &vert->color);
      tr_parse_offset_color_rgba(
          tr, param->type6.offset_color_r, param->type6.offset_color_g,
          param->type6.offset_color_b, param->type6.offset_color_a,
          &vert->offset_color);
      uint32_t u = param->type6.uv[0] << 16;
      uint32_t v = param->type6.uv[0] << 16;
      vert->uv[0] = *(float *)&u;
      vert->uv[1] = *(float *)&v;
    } break;

    case 7: {
      struct vertex *vert = tr_alloc_vert(tr, rctx);
      vert->xyz[0] = param->type7.xyz[0];
      vert->xyz[1] = param->type7.xyz[1];
      vert->xyz[2] = param->type7.xyz[2];
      tr_parse_color_intensity(tr, param->type7.base_intensity, &vert->color);
      tr_parse_offset_color_intensity(tr, param->type7.offset_intensity,
                                      &vert->offset_color);
      vert->uv[0] = param->type7.uv[0];
      vert->uv[1] = param->type7.uv[1];
    } break;

    case 8: {
      struct vertex *vert = tr_alloc_vert(tr, rctx);
      vert->xyz[0] = param->type8.xyz[0];
      vert->xyz[1] = param->type8.xyz[1];
      vert->xyz[2] = param->type8.xyz[2];
      tr_parse_color_intensity(tr, param->type8.base_intensity, &vert->color);
      tr_parse_offset_color_intensity(tr, param->type8.offset_intensity,
                                      &vert->offset_color);
      uint32_t u = param->type8.uv[0] << 16;
      uint32_t v = param->type8.uv[0] << 16;
      vert->uv[0] = *(float *)&u;
      vert->uv[1] = *(float *)&v;
    } break;

    case 15: {
      tr_parse_spritea_vert(tr, param, 0, tr_alloc_vert(tr, rctx));
      tr_parse_spritea_vert(tr, param, 1, tr_alloc_vert(tr, rctx));
      tr_parse_spritea_vert(tr, param, 3, tr_alloc_vert(tr, rctx));
      tr_parse_spritea_vert(tr, param, 2, tr_alloc_vert(tr, rctx));
    } break;

    case 16: {
      tr_parse_spriteb_vert(tr, param, 0, tr_alloc_vert(tr, rctx));
      tr_parse_spriteb_vert(tr, param, 1, tr_alloc_vert(tr, rctx));
      tr_parse_spriteb_vert(tr, param, 3, tr_alloc_vert(tr, rctx));
      tr_parse_spriteb_vert(tr, param, 2, tr_alloc_vert(tr, rctx));
    } break;

    case 17: {
      LOG_WARNING("Unhandled modvol triangle");
    } break;

    default:
      LOG_FATAL("Unsupported vertex type %d", tr->vertex_type);
      break;
  }

  // In the case of the Polygon type, the last struct vertex Parameter for an
  // object
  // must have "End of Strip" specified.  If Vertex Parameters with the "End
  // of Strip" specification were not input, but parameters other than the
  // Vertex Parameters were input, the polygon data in question is ignored and
  // an interrupt signal is output.
  // FIXME is this true for sprites which come through this path as well?
}

static float tr_cmp_surf(struct render_ctx *rctx, const struct surface *a,
                         const struct surface *b) {
  float minza = FLT_MAX;
  for (int i = 0, n = a->num_verts; i < n; i++) {
    struct vertex *v = &rctx->verts[a->first_vert + i];

    if (v->xyz[2] < minza) {
      minza = v->xyz[2];
    }
  }

  float minzb = FLT_MAX;
  for (int i = 0, n = b->num_verts; i < n; i++) {
    struct vertex *v = &rctx->verts[b->first_vert + i];

    if (v->xyz[2] < minzb) {
      minzb = v->xyz[2];
    }
  }

  return minza - minzb;
}

static void tr_merge_surfs(struct render_ctx *rctx, int *low, int *mid,
                           int *high) {
  static int tmp[16384];

  int *i = low;
  int *j = mid + 1;
  int *k = tmp;
  int *end = tmp + array_size(tmp);

  while (i <= mid && j <= high) {
    DCHECK_LT(k, end);
    if (tr_cmp_surf(rctx, &rctx->surfs[*i], &rctx->surfs[*j]) <= 0.0f) {
      *(k++) = *(i++);
    } else {
      *(k++) = *(j++);
    }
  }

  while (i <= mid) {
    DCHECK_LT(k, end);
    *(k++) = *(i++);
  }

  while (j <= high) {
    DCHECK_LT(k, end);
    *(k++) = *(j++);
  }

  memcpy(low, tmp, (k - tmp) * sizeof(tmp[0]));
}

static void tr_sort_surfs(struct render_ctx *rctx, int low, int high) {
  if (low >= high) {
    return;
  }

  int mid = (low + high) / 2;
  tr_sort_surfs(rctx, low, mid);
  tr_sort_surfs(rctx, mid + 1, high);
  tr_merge_surfs(rctx, &rctx->sorted_surfs[low], &rctx->sorted_surfs[mid],
                 &rctx->sorted_surfs[high]);
}

static void tr_parse_eol(struct tr *tr, const struct tile_ctx *ctx,
                         struct render_ctx *rctx, const uint8_t *data) {
  tr_discard_incomplete_surf(tr, rctx);

  // sort transparent polys by their z value, from back to front. remember, in
  // dreamcast coordinates smaller z values are further away from the camera
  if ((tr->list_type == TA_LIST_TRANSLUCENT ||
       tr->list_type == TA_LIST_TRANSLUCENT_MODVOL) &&
      ctx->autosort) {
    tr_sort_surfs(rctx, tr->last_sorted_surf, rctx->num_surfs - 1);
  }

  tr->last_poly = NULL;
  tr->last_vertex = NULL;
  tr->last_sorted_surf = rctx->num_surfs;
}

// Vertices coming into the TA are in window space, with the Z component being
// 1/W. These coordinates need to be converted back to clip space in order to
// be rendered with OpenGL, etc. While we want to perform an orthographic
// projection on the vertices as they're already perspective correct, the
// renderer backend will have to deal with setting the W component of each
// in order to perspective correct the texture mapping.
static void tr_proj_mat(struct tr *tr, const struct tile_ctx *ctx,
                        struct render_ctx *rctx) {
  float znear = FLT_MIN;
  float zfar = FLT_MAX;

  // Z component is 1/W, so +Z is into the screen
  for (int i = 0; i < rctx->num_verts; i++) {
    struct vertex *v = &rctx->verts[i];
    if (v->xyz[2] > znear) {
      znear = v->xyz[2];
    }
    if (v->xyz[2] < zfar) {
      zfar = v->xyz[2];
    }
  }

  // fudge so Z isn't being mapped to exactly 0.0 and 1.0
  float zdepth = (znear - zfar) * 1.1f;

  // fix case where a single polygon being renderered
  if (zdepth <= 0.0f) {
    zdepth = 1.0f;
  }

  // convert from window space coordinates into clip space
  rctx->projection[0] = 2.0f / (float)ctx->video_width;
  rctx->projection[4] = 0.0f;
  rctx->projection[8] = 0.0f;
  rctx->projection[12] = -1.0f;

  rctx->projection[1] = 0.0f;
  rctx->projection[5] = -2.0f / (float)ctx->video_height;
  rctx->projection[9] = 0.0f;
  rctx->projection[13] = 1.0f;

  rctx->projection[2] = 0.0f;
  rctx->projection[6] = 0.0f;
  rctx->projection[10] = (-znear - zfar) / zdepth;
  rctx->projection[14] = (2.0f * zfar * znear) / zdepth;

  rctx->projection[3] = 0.0f;
  rctx->projection[7] = 0.0f;
  rctx->projection[11] = 0.0f;
  rctx->projection[15] = 1.0f;
}

static void tr_reset(struct tr *tr, struct render_ctx *rctx) {
  // reset render state
  rctx->num_surfs = 0;
  rctx->num_verts = 0;
  rctx->num_states = 0;

  // reset global state
  tr->last_poly = NULL;
  tr->last_vertex = NULL;
  tr->list_type = 0;
  tr->vertex_type = 0;
  tr->last_sorted_surf = 0;
}

texture_key_t tr_get_texture_key(union tsp tsp, union tcw tcw) {
  return ((uint64_t)tsp.full << 32) | tcw.full;
}

static void tr_parse_context_inner(struct tr *tr, const struct tile_ctx *ctx,
                                   struct render_ctx *rctx) {
  const uint8_t *data = ctx->data;
  const uint8_t *end = ctx->data + ctx->size;

  tr_reset(tr, rctx);

  tr_parse_bg(tr, ctx, rctx);

  while (data < end) {
    union pcw pcw = *(union pcw *)data;

    // FIXME
    // If Vertex Parameters with the "End of Strip" specification were not
    // input, but parameters other than the Vertex Parameters were input, the
    // polygon data in question is ignored and an interrupt signal is output.

    switch (pcw.para_type) {
      // control params
      case TA_PARAM_END_OF_LIST:
        tr_parse_eol(tr, ctx, rctx, data);
        break;

      case TA_PARAM_USER_TILE_CLIP:
        // nothing to do
        break;

      case TA_PARAM_OBJ_LIST_SET:
        LOG_FATAL("TA_PARAM_OBJ_LIST_SET unsupported");
        break;

      // global params
      case TA_PARAM_POLY_OR_VOL:
        tr_parse_poly_param(tr, ctx, rctx, data);
        break;

      case TA_PARAM_SPRITE:
        tr_parse_poly_param(tr, ctx, rctx, data);
        break;

      // vertex params
      case TA_PARAM_VERTEX:
        tr_parse_vert_param(tr, ctx, rctx, data);
        break;

      default:
        LOG_FATAL("Unsupported parameter type %d", pcw.para_type);
        break;
    }

    // keep track of the surf / vert counts at each parameter offset
    if (rctx->states) {
      int offset = (int)(data - ctx->data);
      CHECK_LT(offset, rctx->states_size);
      rctx->num_states = MAX(rctx->num_states, offset);

      struct param_state *param_state = &rctx->states[offset];
      param_state->num_surfs = rctx->num_surfs;
      param_state->num_verts = rctx->num_verts;
    }

    data += ta_get_param_size(pcw, tr->vertex_type);
  }

  tr_proj_mat(tr, ctx, rctx);
}

void tr_parse_context(struct tr *tr, const struct tile_ctx *ctx,
                      struct render_ctx *rctx) {
  PROF_ENTER("tr_parse_context");

  tr_parse_context_inner(tr, ctx, rctx);

  PROF_LEAVE();
}

static void tr_render_context_inner(struct tr *tr,
                                    const struct render_ctx *ctx) {
  rb_begin_surfaces(tr->rb, ctx->projection, ctx->verts, ctx->num_verts);

  const int *sorted_surf = ctx->sorted_surfs;
  const int *sorted_surf_end = ctx->sorted_surfs + ctx->num_surfs;
  while (sorted_surf < sorted_surf_end) {
    rb_draw_surface(tr->rb, &ctx->surfs[*sorted_surf]);
    sorted_surf++;
  }

  rb_end_surfaces(tr->rb);
}

void tr_render_context(struct tr *tr, const struct render_ctx *ctx) {
  PROF_ENTER("tr_render_context");

  tr_render_context_inner(tr, ctx);

  PROF_LEAVE();
}

struct tr *tr_create(struct rb *rb, void *get_texture_data,
                     get_texture_cb get_texture) {
  struct tr *tr = malloc(sizeof(struct tr));
  memset(tr, 0, sizeof(*tr));
  tr->rb = rb;
  tr->get_texture_data = get_texture_data;
  tr->get_texture = get_texture;

  return tr;
}

void tr_destroy(struct tr *tr) {
  free(tr);
}
