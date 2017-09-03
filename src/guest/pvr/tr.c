#include "guest/pvr/tr.h"
#include "core/assert.h"
#include "core/core.h"
#include "core/math.h"
#include "core/profiler.h"
#include "core/sort.h"
#include "guest/pvr/pixel_convert.h"
#include "guest/pvr/ta.h"

struct tr {
  struct render_backend *r;
  void *userdata;
  tr_find_texture_cb find_texture;

  /* current global state */
  const union poly_param *last_poly;
  const union vert_param *last_vertex;
  int list_type;
  int vertex_type;
  float face_color[4];
  float face_offset_color[4];
  int merged_surfs;
};

static int compressed_mipmap_offsets[] = {
    0x00006, /* 8 x 8 */
    0x00016, /* 16 x 16 */
    0x00056, /* 32 x 32 */
    0x00156, /* 64 x 64 */
    0x00556, /* 128 x 128 */
    0x01556, /* 256 x 256 */
    0x05556, /* 512 x 512 */
    0x15556, /* 1024 x 1024 */
};

static int paletted_4bpp_mipmap_offsets[] = {
    0x0000c, /* 8 x 8 */
    0x0002c, /* 16 x 16 */
    0x000ac, /* 32 x 32 */
    0x002ac, /* 64 x 64 */
    0x00aac, /* 128 x 128 */
    0x02aac, /* 256 x 256 */
    0x0aaac, /* 512 x 512 */
    0x2aaac, /* 1024 x 1024 */
};

static int paletted_8bpp_mipmap_offsets[] = {
    0x00018, /* 8 x 8 */
    0x00058, /* 16 x 16 */
    0x00158, /* 32 x 32 */
    0x00558, /* 64 x 64 */
    0x01558, /* 128 x 128 */
    0x05558, /* 256 x 256 */
    0x15558, /* 512 x 512 */
    0x55558, /* 1024 x 1024 */
};

static int nonpaletted_mipmap_offsets[] = {
    0x00030, /* 8 x 8 */
    0x000b0, /* 16 x 16 */
    0x002b0, /* 32 x 32 */
    0x00ab0, /* 64 x 64 */
    0x02ab0, /* 128 x 128 */
    0x0aab0, /* 256 x 256 */
    0x2aab0, /* 512 x 512 */
    0xaaab0, /* 1024 x 1024 */
};

static inline enum depth_func translate_depth_func(uint32_t depth_func) {
  static enum depth_func depth_funcs[] = {
      DEPTH_NEVER, DEPTH_GREATER, DEPTH_EQUAL,  DEPTH_GEQUAL,
      DEPTH_LESS,  DEPTH_NEQUAL,  DEPTH_LEQUAL, DEPTH_ALWAYS};
  return depth_funcs[depth_func];
}

static inline enum cull_face translate_cull(uint32_t cull_mode) {
  static enum cull_face cull_modes[] = {CULL_NONE, CULL_NONE, CULL_BACK,
                                        CULL_FRONT};
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

static texture_handle_t tr_convert_texture(struct tr *tr,
                                           const struct tile_context *ctx,
                                           union tsp tsp, union tcw tcw) {
  PROF_ENTER("gpu", "tr_convert_texture");

  /* TODO it's bad that textures are only cached based off tsp / tcw yet the
     TEXT_CONTROL registers and PAL_RAM_CTRL registers are used here to control
     texture generation */

  struct tr_texture *entry = tr->find_texture(tr->userdata, tsp, tcw);
  CHECK_NOTNULL(entry);

  /* if there's a non-dirty handle, return it */
  if (entry->handle && !entry->dirty) {
    PROF_LEAVE();
    return entry->handle;
  }

  /* if there's a dirty handle, destroy it before creating the new one */
  if (entry->handle && entry->dirty) {
    r_destroy_texture(tr->r, entry->handle);
    entry->handle = 0;
  }

  static uint8_t converted[1024 * 1024 * 4];
  const uint8_t *palette = entry->palette;
  const uint8_t *texture = entry->texture;
  const uint8_t *input = texture;
  const uint8_t *output = texture;

  /* textures are either twiddled and vq compressed, twiddled and uncompressed
     or planar */
  int twiddled = ta_texture_twiddled(tcw);
  int compressed = ta_texture_compressed(tcw);
  int mipmaps = ta_texture_mipmaps(tcw);

  /* get texture dimensions */
  int width = ta_texture_width(tsp, tcw);
  int height = ta_texture_height(tsp, tcw);
  int stride = width;
  if (!twiddled && tcw.stride_select) {
    stride = ctx->stride;
  }

  /* mipmap textures contain data for 1 x 1 up to width x height. skip to the
     highest res and let the renderer backend generate its own mipmaps */
  if (mipmaps) {
    if (compressed) {
      /* for vq compressed textures the offset is only for the index data, the
         codebook is the same for all levels */
      input += compressed_mipmap_offsets[tsp.texture_u_size];
    } else if (tcw.pixel_format == TA_PIXEL_4BPP) {
      input += paletted_4bpp_mipmap_offsets[tsp.texture_u_size];
    } else if (tcw.pixel_format == TA_PIXEL_8BPP) {
      input += paletted_8bpp_mipmap_offsets[tsp.texture_u_size];
    } else {
      input += nonpaletted_mipmap_offsets[tsp.texture_u_size];
    }
  }

  /* used by vq compressed textures */
  const uint8_t *codebook = texture;
  const uint8_t *index = input + TA_CODEBOOK_SIZE;

  switch (tcw.pixel_format) {
    case TA_PIXEL_1555:
    case TA_PIXEL_RESERVED:
      output = converted;
      if (compressed) {
        convert_vq_ARGB1555_RGBA(codebook, index, (uint32_t *)converted, width,
                                 height);
      } else if (twiddled) {
        convert_twiddled_ARGB1555_RGBA((const uint16_t *)input,
                                       (uint32_t *)converted, width, height);
      } else {
        convert_planar_ARGB1555_RGBA((const uint16_t *)input,
                                     (uint32_t *)converted, width, height,
                                     stride);
      }
      break;

    case TA_PIXEL_565:
      output = converted;
      if (compressed) {
        convert_vq_RGB565_RGBA(codebook, index, (uint32_t *)converted, width,
                               height);
      } else if (twiddled) {
        convert_twiddled_RGB565_RGBA((const uint16_t *)input,
                                     (uint32_t *)converted, width, height);
      } else {
        convert_planar_RGB565_RGBA((const uint16_t *)input,
                                   (uint32_t *)converted, width, height,
                                   stride);
      }
      break;

    case TA_PIXEL_4444:
      output = converted;
      if (compressed) {
        convert_vq_ARGB4444_RGBA(codebook, index, (uint32_t *)converted, width,
                                 height);
      } else if (twiddled) {
        convert_twiddled_ARGB4444_RGBA((const uint16_t *)input,
                                       (uint32_t *)converted, width, height);
      } else {
        convert_planar_ARGB4444_RGBA((const uint16_t *)input,
                                     (uint32_t *)converted, width, height,
                                     stride);
      }
      break;

    case TA_PIXEL_YUV422:
      output = converted;
      CHECK(!compressed);
      if (twiddled) {
        convert_twiddled_UYVY422_RGBA((const uint16_t *)input,
                                      (uint32_t *)converted, width, height);

      } else {
        convert_planar_UYVY422_RGBA((const uint16_t *)input,
                                    (uint32_t *)converted, width, height,
                                    stride);
      }
      break;

    case TA_PIXEL_4BPP:
      CHECK(!compressed);
      output = converted;
      switch (ctx->pal_pxl_format) {
        case TA_PAL_ARGB1555:
          convert_pal4_ARGB1555_RGBA(input, (uint32_t *)converted,
                                     (const uint32_t *)palette, width, height);
          break;

        case TA_PAL_RGB565:
          convert_pal4_RGB565_RGBA(input, (uint32_t *)converted,
                                   (const uint32_t *)palette, width, height);
          break;

        case TA_PAL_ARGB4444:
          convert_pal4_ARGB4444_RGBA(input, (uint32_t *)converted,
                                     (const uint32_t *)palette, width, height);
          break;

        case TA_PAL_ARGB8888:
          convert_pal4_ARGB8888_RGBA(input, (uint32_t *)converted,
                                     (const uint32_t *)palette, width, height);

          break;

        default:
          LOG_FATAL("unsupported 4bpp palette pixel format %d",
                    ctx->pal_pxl_format);
          break;
      }
      break;

    case TA_PIXEL_8BPP:
      CHECK(!compressed);
      output = converted;
      switch (ctx->pal_pxl_format) {
        case TA_PAL_ARGB1555:
          convert_pal8_ARGB1555_RGBA(input, (uint32_t *)converted,
                                     (const uint32_t *)palette, width, height);
          break;

        case TA_PAL_RGB565:
          convert_pal8_RGB565_RGBA(input, (uint32_t *)converted,
                                   (const uint32_t *)palette, width, height);
          break;

        case TA_PAL_ARGB4444:
          convert_pal8_ARGB4444_RGBA(input, (uint32_t *)converted,
                                     (const uint32_t *)palette, width, height);
          break;

        case TA_PAL_ARGB8888:
          convert_pal8_ARGB8888_RGBA(input, (uint32_t *)converted,
                                     (const uint32_t *)palette, width, height);
          break;

        default:
          LOG_FATAL("unsupported 8bpp palette pixel format %d",
                    ctx->pal_pxl_format);
          break;
      }
      break;

    default:
      LOG_FATAL("unsupported tcw pixel format %d", tcw.pixel_format);
      break;
  }

  /* ignore trilinear filtering for now */
  enum filter_mode filter =
      tsp.filter_mode == 0 ? FILTER_NEAREST : FILTER_BILINEAR;
  enum wrap_mode wrap_u =
      tsp.clamp_u ? WRAP_CLAMP_TO_EDGE
                  : (tsp.flip_u ? WRAP_MIRRORED_REPEAT : WRAP_REPEAT);
  enum wrap_mode wrap_v =
      tsp.clamp_v ? WRAP_CLAMP_TO_EDGE
                  : (tsp.flip_v ? WRAP_MIRRORED_REPEAT : WRAP_REPEAT);

  entry->handle = r_create_texture(tr->r, PXL_RGBA, filter, wrap_u, wrap_v,
                                   mipmaps, width, height, output);
  entry->format = PXL_RGBA;
  entry->filter = filter;
  entry->wrap_u = wrap_u;
  entry->wrap_v = wrap_v;
  entry->width = width;
  entry->height = height;
  entry->dirty = 0;

  PROF_LEAVE();

  return entry->handle;
}

static struct ta_surface *tr_reserve_surf(struct tr *tr, struct tr_context *rc,
                                          int copy_from_prev) {
  int surf_index = rc->num_surfs;

  CHECK_LT(surf_index, ARRAY_SIZE(rc->surfs));
  struct ta_surface *surf = &rc->surfs[surf_index];

  if (copy_from_prev) {
    CHECK(rc->num_surfs);
    *surf = rc->surfs[rc->num_surfs - 1];
  } else {
    memset(surf, 0, sizeof(*surf));
  }

  surf->first_vert = rc->num_indices;
  surf->num_verts = 0;

  return surf;
}

static struct ta_vertex *tr_reserve_vert(struct tr *tr, struct tr_context *rc) {
  struct ta_surface *curr_surf = &rc->surfs[rc->num_surfs];
  int curr_surf_vert = curr_surf->num_verts / 3;

  int vert_index = rc->num_verts + curr_surf_vert;
  CHECK_LT(vert_index, ARRAY_SIZE(rc->verts));
  struct ta_vertex *vert = &rc->verts[vert_index];

  int index = rc->num_indices + curr_surf->num_verts;
  CHECK_LT(index + 2, ARRAY_SIZE(rc->indices));
  uint16_t *indices = &rc->indices[index];

  memset(vert, 0, sizeof(*vert));

  /* polygons are fed to the TA as triangle strips, with the vertices being fed
     in a CW order, so a given quad looks like:

     1----3----5
     |\   |\   |
     | \  | \  |
     |  \ |  \ |
     |   \|   \|
     0----2----4

     convert from these triangle strips to triangles to make merging surfaces
     easy in tr_commit_surf, and convert to CCW to match OpenGL defaults */
  if (curr_surf_vert & 1) {
    indices[0] = vert_index;
    indices[1] = vert_index + 1;
    indices[2] = vert_index + 2;
  } else {
    indices[0] = vert_index;
    indices[1] = vert_index + 2;
    indices[2] = vert_index + 1;
  }

  curr_surf->num_verts += 3;

  return vert;
}

static inline int tr_can_merge_surfs(struct ta_surface *a,
                                     struct ta_surface *b) {
  return a->texture == b->texture && a->depth_write == b->depth_write &&
         a->depth_func == b->depth_func && a->cull == b->cull &&
         a->src_blend == b->src_blend && a->dst_blend == b->dst_blend &&
         a->shade == b->shade && a->ignore_alpha == b->ignore_alpha &&
         a->ignore_texture_alpha == b->ignore_texture_alpha &&
         a->offset_color == b->offset_color &&
         a->pt_alpha_test == b->pt_alpha_test &&
         a->pt_alpha_ref == b->pt_alpha_ref;
}

static void tr_commit_surf(struct tr *tr, struct tr_context *rc) {
  struct ta_surface *new_surf = &rc->surfs[rc->num_surfs];

  /* tr_reserve_vert preemptively adds indices for the next two vertices when
     converting the incoming triangle strips to triangles. this results in the
     first 2 vertices adding 6 extra indices that don't exist */
  new_surf->num_verts -= 6;

  /* check to see if this surface can be merged with the previous surface */
  struct ta_surface *prev_surf = NULL;

  if (rc->num_surfs) {
    prev_surf = &rc->surfs[rc->num_surfs - 1];
  }

  if (prev_surf && tr_can_merge_surfs(prev_surf, new_surf)) {
    /* merge the new verts into the prev surface */
    prev_surf->num_verts += new_surf->num_verts;

    tr->merged_surfs++;
  } else {
    /* default sort the new surface */
    struct tr_list *list = &rc->lists[tr->list_type];
    list->surfs[list->num_surfs] = rc->num_surfs;
    list->num_surfs++;

    /* commit the new surface */
    rc->num_surfs += 1;
  }

  /* commit the new verts and indices */
  rc->num_verts += (new_surf->num_verts + 6) / 3;
  rc->num_indices += new_surf->num_verts;
}

/*
* polygon parsing helpers
*/
#define PARSE_XYZ(x, y, z, xyz) \
  {                             \
    xyz[0] = (x);               \
    xyz[1] = (y);               \
    xyz[2] = (z);               \
  }

#define PARSE_COLOR(base_color, color) \
  { *color = abgr_to_rgba(base_color); }

#define PARSE_COLOR_RGBA(r, g, b, a, color) \
  { *color = float_to_rgba(r, g, b, a); }

#define PARSE_COLOR_INTENSITY(base_intensity, color)                          \
  {                                                                           \
    *color =                                                                  \
        float_to_rgba(tr->face_color[0] * base_intensity,                     \
                      tr->face_color[1] * base_intensity,                     \
                      tr->face_color[2] * base_intensity, tr->face_color[3]); \
  }

#define PARSE_OFFSET_COLOR(offset_color, color) \
  { *color = abgr_to_rgba(offset_color); }

#define PARSE_OFFSET_COLOR_RGBA(r, g, b, a, color) \
  { *color = float_to_rgba(r, g, b, a); }

#define PARSE_OFFSET_COLOR_INTENSITY(offset_intensity, color)           \
  {                                                                     \
    *color = float_to_rgba(tr->face_offset_color[0] * offset_intensity, \
                           tr->face_offset_color[1] * offset_intensity, \
                           tr->face_offset_color[2] * offset_intensity, \
                           tr->face_offset_color[3]);                   \
  }

static int tr_parse_bg_vert(const struct tile_context *ctx,
                            struct tr_context *rc, int offset,
                            struct ta_vertex *v) {
  PARSE_XYZ(*(float *)&ctx->bg_vertices[offset],
            *(float *)&ctx->bg_vertices[offset + 4],
            *(float *)&ctx->bg_vertices[offset + 8], v->xyz);
  offset += 12;

  if (ctx->bg_isp.texture) {
    LOG_FATAL("unsupported bg_isp.texture");
    /*v->uv[0] = *(float *)(&ctx->bg_vertices[offset]);
    v->uv[1] = *(float *)(&ctx->bg_vertices[offset + 4]);
    offset += 8;*/
  }

  uint32_t base_color = *(uint32_t *)&ctx->bg_vertices[offset];
  v->color = abgr_to_rgba(base_color);
  offset += 4;

  if (ctx->bg_isp.offset) {
    LOG_FATAL("unsupported bg_isp.offset");
    /*uint32_t offset_color = *(uint32_t *)(&ctx->bg_vertices[offset]);
    v->offset_color[0] = ((offset_color >> 16) & 0xff) / 255.0f;
    v->offset_color[1] = ((offset_color >> 16) & 0xff) / 255.0f;
    v->offset_color[2] = ((offset_color >> 16) & 0xff) / 255.0f;
    v->offset_color[3] = 0.0f;
    offset += 4;*/
  }

  return offset;
}

static void tr_parse_bg(struct tr *tr, const struct tile_context *ctx,
                        struct tr_context *rc) {
  tr->list_type = TA_LIST_OPAQUE;

  /* translate the surface */
  struct ta_surface *surf = tr_reserve_surf(tr, rc, 0);
  surf->texture = 0;
  surf->depth_write = !ctx->bg_isp.z_write_disable;
  surf->depth_func = translate_depth_func(ctx->bg_isp.depth_compare_mode);
  surf->cull = translate_cull(ctx->bg_isp.culling_mode);
  surf->src_blend = BLEND_NONE;
  surf->dst_blend = BLEND_NONE;

  /* translate the first 3 vertices */
  struct ta_vertex *v0 = tr_reserve_vert(tr, rc);
  struct ta_vertex *v1 = tr_reserve_vert(tr, rc);
  struct ta_vertex *v2 = tr_reserve_vert(tr, rc);
  struct ta_vertex *v3 = tr_reserve_vert(tr, rc);

  int offset = 0;
  offset = tr_parse_bg_vert(ctx, rc, offset, v0);
  offset = tr_parse_bg_vert(ctx, rc, offset, v1);
  offset = tr_parse_bg_vert(ctx, rc, offset, v2);

  /* override xyz values supplied by ISP_BACKGND_T. while the hardware docs act
     like they should be correct, they're most definitely not in most cases */
  PARSE_XYZ(0.0f, (float)ctx->video_height, ctx->bg_depth, v0->xyz);
  PARSE_XYZ(0.0f, 0.0f, ctx->bg_depth, v1->xyz);
  PARSE_XYZ((float)ctx->video_width, (float)ctx->video_height, ctx->bg_depth,
            v2->xyz);

  /* 4th vertex isn't supplied, fill it out automatically */
  PARSE_XYZ(v2->xyz[0], v1->xyz[1], ctx->bg_depth, v3->xyz);
  v3->color = v0->color;
  v3->offset_color = v0->offset_color;
  v3->uv[0] = v2->uv[0];
  v3->uv[1] = v1->uv[1];

  tr_commit_surf(tr, rc);

  tr->list_type = TA_NUM_LISTS;
}

/* this offset color implementation is not correct at all, see the
   Texture/Shading Instruction in the union tsp instruction word */
static void tr_parse_poly_param(struct tr *tr, const struct tile_context *ctx,
                                struct tr_context *rc, const uint8_t *data) {
  const union poly_param *param = (const union poly_param *)data;

  /* reset state */
  tr->last_poly = param;
  tr->last_vertex = NULL;
  tr->vertex_type = ta_get_vert_type(param->type0.pcw);

  int poly_type = ta_get_poly_type(param->type0.pcw);

  if (poly_type == 6) {
    /* FIXME handle modifier volumes */
    return;
  }

  switch (poly_type) {
    case 0: {
      /*uint32_t sdma_data_size;
      uint32_t sdma_next_addr;*/
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

    default:
      LOG_FATAL("unsupported poly type %d", poly_type);
      break;
  }

  /* setup the new surface */
  struct ta_surface *surf = tr_reserve_surf(tr, rc, 0);
  surf->depth_write = !param->type0.isp_tsp.z_write_disable;
  surf->depth_func =
      translate_depth_func(param->type0.isp_tsp.depth_compare_mode);
  surf->cull = translate_cull(param->type0.isp_tsp.culling_mode);
  surf->src_blend = translate_src_blend_func(param->type0.tsp.src_alpha_instr);
  surf->dst_blend = translate_dst_blend_func(param->type0.tsp.dst_alpha_instr);
  surf->shade = translate_shade_mode(param->type0.tsp.texture_shading_instr);
  surf->ignore_alpha = !param->type0.tsp.use_alpha;
  surf->ignore_texture_alpha = param->type0.tsp.ignore_tex_alpha;
  surf->offset_color = param->type0.isp_tsp.offset;
  surf->pt_alpha_test = tr->list_type == TA_LIST_PUNCH_THROUGH;
  surf->pt_alpha_ref = (float)ctx->pt_alpha_ref / 0xff;

  /* override a few surface parameters based on the list type */
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
    surf->texture =
        tr_convert_texture(tr, ctx, param->type0.tsp, param->type0.tcw);
  } else {
    surf->texture = 0;
  }
}

static void tr_parse_vert_param(struct tr *tr, const struct tile_context *ctx,
                                struct tr_context *rc, const uint8_t *data) {
  const union vert_param *param = (const union vert_param *)data;

  if (tr->vertex_type == 17) {
    /* FIXME handle modifier volumes */
    return;
  }

  /* if there is no need to change the Global Parameters, a Vertex Parameter
     for the next polygon may be input immediately after inputting a Vertex
     Parameter for which "End of Strip" was specified */
  if (tr->last_vertex && tr->last_vertex->type0.pcw.end_of_strip) {
    tr_reserve_surf(tr, rc, 1);
  }
  tr->last_vertex = param;

  switch (tr->vertex_type) {
    case 0: {
      struct ta_vertex *vert = tr_reserve_vert(tr, rc);
      PARSE_XYZ(param->type0.xyz[0], param->type0.xyz[1], param->type0.xyz[2],
                vert->xyz);
      PARSE_COLOR(param->type0.base_color, &vert->color);
      vert->offset_color = 0;
      vert->uv[0] = 0.0f;
      vert->uv[1] = 0.0f;
    } break;

    case 1: {
      struct ta_vertex *vert = tr_reserve_vert(tr, rc);
      PARSE_XYZ(param->type1.xyz[0], param->type1.xyz[1], param->type1.xyz[2],
                vert->xyz);
      PARSE_COLOR_RGBA(param->type1.base_color_r, param->type1.base_color_g,
                       param->type1.base_color_b, param->type1.base_color_a,
                       &vert->color);
      vert->offset_color = 0;
      vert->uv[0] = 0.0f;
      vert->uv[1] = 0.0f;
    } break;

    case 2: {
      struct ta_vertex *vert = tr_reserve_vert(tr, rc);
      PARSE_XYZ(param->type2.xyz[0], param->type2.xyz[1], param->type2.xyz[2],
                vert->xyz);
      PARSE_COLOR_INTENSITY(param->type2.base_intensity, &vert->color);
      vert->offset_color = 0;
      vert->uv[0] = 0.0f;
      vert->uv[1] = 0.0f;
    } break;

    case 3: {
      struct ta_vertex *vert = tr_reserve_vert(tr, rc);
      PARSE_XYZ(param->type3.xyz[0], param->type3.xyz[1], param->type3.xyz[2],
                vert->xyz);
      PARSE_COLOR(param->type3.base_color, &vert->color);
      PARSE_OFFSET_COLOR(param->type3.offset_color, &vert->offset_color);
      vert->uv[0] = param->type3.uv[0];
      vert->uv[1] = param->type3.uv[1];
    } break;

    case 4: {
      struct ta_vertex *vert = tr_reserve_vert(tr, rc);
      PARSE_XYZ(param->type4.xyz[0], param->type4.xyz[1], param->type4.xyz[2],
                vert->xyz);
      PARSE_COLOR(param->type4.base_color, &vert->color);
      PARSE_OFFSET_COLOR(param->type4.offset_color, &vert->offset_color);
      uint32_t u = param->type4.vu[1] << 16;
      uint32_t v = param->type4.vu[0] << 16;
      vert->uv[0] = *(float *)&u;
      vert->uv[1] = *(float *)&v;
    } break;

    case 5: {
      struct ta_vertex *vert = tr_reserve_vert(tr, rc);
      PARSE_XYZ(param->type5.xyz[0], param->type5.xyz[1], param->type5.xyz[2],
                vert->xyz);
      PARSE_COLOR_RGBA(param->type5.base_color_r, param->type5.base_color_g,
                       param->type5.base_color_b, param->type5.base_color_a,
                       &vert->color);
      PARSE_OFFSET_COLOR_RGBA(param->type5.offset_color_r,
                              param->type5.offset_color_g,
                              param->type5.offset_color_b,
                              param->type5.offset_color_a, &vert->offset_color);
      vert->uv[0] = param->type5.uv[0];
      vert->uv[1] = param->type5.uv[1];
    } break;

    case 6: {
      struct ta_vertex *vert = tr_reserve_vert(tr, rc);
      PARSE_XYZ(param->type6.xyz[0], param->type6.xyz[1], param->type6.xyz[2],
                vert->xyz);
      PARSE_COLOR_RGBA(param->type6.base_color_r, param->type6.base_color_g,
                       param->type6.base_color_b, param->type6.base_color_a,
                       &vert->color);
      PARSE_OFFSET_COLOR_RGBA(param->type6.offset_color_r,
                              param->type6.offset_color_g,
                              param->type6.offset_color_b,
                              param->type6.offset_color_a, &vert->offset_color);
      uint32_t u = param->type6.vu[1] << 16;
      uint32_t v = param->type6.vu[0] << 16;
      vert->uv[0] = *(float *)&u;
      vert->uv[1] = *(float *)&v;
    } break;

    case 7: {
      struct ta_vertex *vert = tr_reserve_vert(tr, rc);
      PARSE_XYZ(param->type7.xyz[0], param->type7.xyz[1], param->type7.xyz[2],
                vert->xyz);
      PARSE_COLOR_INTENSITY(param->type7.base_intensity, &vert->color);
      PARSE_OFFSET_COLOR_INTENSITY(param->type7.offset_intensity,
                                   &vert->offset_color);
      vert->uv[0] = param->type7.uv[0];
      vert->uv[1] = param->type7.uv[1];
    } break;

    case 8: {
      struct ta_vertex *vert = tr_reserve_vert(tr, rc);
      PARSE_XYZ(param->type8.xyz[0], param->type8.xyz[1], param->type8.xyz[2],
                vert->xyz);
      PARSE_COLOR_INTENSITY(param->type8.base_intensity, &vert->color);
      PARSE_OFFSET_COLOR_INTENSITY(param->type8.offset_intensity,
                                   &vert->offset_color);
      uint32_t u = param->type8.vu[1] << 16;
      uint32_t v = param->type8.vu[0] << 16;
      vert->uv[0] = *(float *)&u;
      vert->uv[1] = *(float *)&v;
    } break;

    case 15: {
      CHECK(param->type0.pcw.end_of_strip);

      static const int indices[] = {0, 1, 3, 2};

      for (int i = 0, l = ARRAY_SIZE(indices); i < l; i++) {
        int idx = indices[i];
        struct ta_vertex *vert = tr_reserve_vert(tr, rc);

        /* FIXME this is assuming all sprites are billboards */
        PARSE_XYZ(param->sprite0.xyz[idx][0], param->sprite0.xyz[idx][1],
                  param->sprite0.xyz[0][2], vert->xyz);
        PARSE_COLOR_RGBA(tr->face_color[0], tr->face_color[1],
                         tr->face_color[2], tr->face_color[3], &vert->color);
        PARSE_OFFSET_COLOR_RGBA(tr->face_offset_color[0],
                                tr->face_offset_color[1],
                                tr->face_offset_color[2],
                                tr->face_offset_color[3], &vert->offset_color);
      }
    } break;

    case 16: {
      CHECK(param->type0.pcw.end_of_strip);

      static const int indices[] = {0, 1, 3, 2};

      for (int i = 0, l = ARRAY_SIZE(indices); i < l; i++) {
        int idx = indices[i];
        struct ta_vertex *vert = tr_reserve_vert(tr, rc);

        /* FIXME this is assuming all sprites are billboards */
        PARSE_XYZ(param->sprite1.xyz[idx][0], param->sprite1.xyz[idx][1],
                  param->sprite1.xyz[0][2], vert->xyz);
        PARSE_COLOR_RGBA(tr->face_color[0], tr->face_color[1],
                         tr->face_color[2], tr->face_color[3], &vert->color);
        PARSE_OFFSET_COLOR_RGBA(tr->face_offset_color[0],
                                tr->face_offset_color[1],
                                tr->face_offset_color[2],
                                tr->face_offset_color[3], &vert->offset_color);
        uint32_t u, v;
        if (idx == 3) {
          u = (param->sprite1.uv[0] & 0xffff0000);
          v = (param->sprite1.uv[2] & 0x0000ffff) << 16;
        } else {
          u = (param->sprite1.uv[idx] & 0xffff0000);
          v = (param->sprite1.uv[idx] & 0x0000ffff) << 16;
        }
        vert->uv[0] = *(float *)&u;
        vert->uv[1] = *(float *)&v;
      }
    } break;

    default:
      LOG_FATAL("unsupported vertex type %d", tr->vertex_type);
      break;
  }

  /* in the case of the Polygon type, the last Vertex Parameter for an object
     must have "End of Strip" specified.  If Vertex Parameters with the "End of
     Strip" specification were not input, but parameters other than the Vertex
     Parameters were input, the polygon data in question is ignored and
     an interrupt signal is output */
  if (param->type0.pcw.end_of_strip) {
    tr_commit_surf(tr, rc);
  }
}

/* scratch buffers used by surface merge sort */
static int sort_tmp[TA_MAX_SURFS];
static float sort_minz[TA_MAX_SURFS];

static int tr_compare_surf(const void *a, const void *b) {
  int i = *(const int *)a;
  int j = *(const int *)b;
  return sort_minz[i] <= sort_minz[j];
}

static void tr_sort_render_list(struct tr *tr, struct tr_context *rc,
                                int list_type) {
  PROF_ENTER("gpu", "tr_sort_render_list");

  /* sort each surface from back to front based on its minz */
  struct tr_list *list = &rc->lists[list_type];

  for (int i = 0; i < list->num_surfs; i++) {
    int surf_index = list->surfs[i];
    struct ta_surface *surf = &rc->surfs[surf_index];
    float *minz = &sort_minz[surf_index];

    /* the surf coordinates have 1/w for z, so smaller values are
      further away from the camera */
    *minz = FLT_MAX;

    for (int j = 0; j < surf->num_verts; j++) {
      int vert_index = rc->indices[surf->first_vert + j];
      struct ta_vertex *vert = &rc->verts[vert_index];
      *minz = MIN(*minz, vert->xyz[2]);
    }
  }

  msort_noalloc(list->surfs, sort_tmp, list->num_surfs, sizeof(int),
                &tr_compare_surf);

  PROF_LEAVE();
}

static void tr_parse_eol(struct tr *tr, const struct tile_context *ctx,
                         struct tr_context *rc, const uint8_t *data) {
  tr->last_poly = NULL;
  tr->last_vertex = NULL;
  tr->list_type = TA_NUM_LISTS;
  tr->vertex_type = TA_NUM_VERTS;
}

static void tr_reset(struct tr *tr, struct tr_context *rc) {
  /* reset global state */
  tr->last_poly = NULL;
  tr->last_vertex = NULL;
  tr->list_type = TA_NUM_LISTS;
  tr->vertex_type = TA_NUM_VERTS;
  tr->merged_surfs = 0;

  /* reset render context state */
  rc->num_params = 0;
  rc->num_surfs = 0;
  rc->num_verts = 0;
  rc->num_indices = 0;
  for (int i = 0; i < TA_NUM_LISTS; i++) {
    struct tr_list *list = &rc->lists[i];
    list->num_surfs = 0;
  }
}

static void tr_render_list(struct render_backend *r,
                           const struct tr_context *rc, int list_type,
                           int end_surf, int *stopped) {
  if (*stopped) {
    return;
  }

  const struct tr_list *list = &rc->lists[list_type];
  const int *sorted_surf = list->surfs;
  const int *sorted_surf_end = list->surfs + list->num_surfs;

  while (sorted_surf < sorted_surf_end) {
    int surf = *(sorted_surf++);

    r_draw_ta_surface(r, &rc->surfs[surf]);

    if (surf == end_surf) {
      *stopped = 1;
      break;
    }
  }
}

void tr_render_context_until(struct render_backend *r,
                             const struct tr_context *rc, int end_surf) {
  PROF_ENTER("gpu", "tr_render_context_until");

  int stopped = 0;

  r_begin_ta_surfaces(r, rc->width, rc->height, rc->verts, rc->num_verts,
                      rc->indices, rc->num_indices);

  tr_render_list(r, rc, TA_LIST_OPAQUE, end_surf, &stopped);
  tr_render_list(r, rc, TA_LIST_PUNCH_THROUGH, end_surf, &stopped);
  tr_render_list(r, rc, TA_LIST_TRANSLUCENT, end_surf, &stopped);

  r_end_ta_surfaces(r);

  PROF_LEAVE();
}

void tr_render_context(struct render_backend *r, const struct tr_context *rc) {
  tr_render_context_until(r, rc, -1);
}

void tr_convert_context(struct render_backend *r, void *userdata,
                        tr_find_texture_cb find_texture,
                        const struct tile_context *ctx, struct tr_context *rc) {
  PROF_ENTER("gpu", "tr_convert_context");

  struct tr tr;
  tr.r = r;
  tr.userdata = userdata;
  tr.find_texture = find_texture;

  const uint8_t *data = ctx->params;
  const uint8_t *end = ctx->params + ctx->size;

  ta_init_tables();

  tr_reset(&tr, rc);

  rc->width = ctx->video_width;
  rc->height = ctx->video_height;

  tr_parse_bg(&tr, ctx, rc);

  while (data < end) {
    union pcw pcw = *(union pcw *)data;

    if (ta_pcw_list_type_valid(pcw, tr.list_type)) {
      tr.list_type = pcw.list_type;
    }

    switch (pcw.para_type) {
      /* control params */
      case TA_PARAM_END_OF_LIST:
        tr_parse_eol(&tr, ctx, rc, data);
        break;

      case TA_PARAM_USER_TILE_CLIP:
        break;

      case TA_PARAM_OBJ_LIST_SET:
        LOG_FATAL("TA_PARAM_OBJ_LIST_SET unsupported");
        break;

      /* global params */
      case TA_PARAM_POLY_OR_VOL:
      case TA_PARAM_SPRITE:
        tr_parse_poly_param(&tr, ctx, rc, data);
        break;

      /* vertex params */
      case TA_PARAM_VERTEX:
        tr_parse_vert_param(&tr, ctx, rc, data);
        break;
    }

    /* track info about the parse state for tracer debugging */
    struct tr_param *rp = &rc->params[rc->num_params++];
    rp->offset = (int)(data - ctx->params);
    rp->list_type = tr.list_type;
    rp->vertex_type = tr.list_type;
    rp->last_surf = rc->num_surfs - 1;
    rp->last_vert = rc->num_verts - 1;

    data += ta_get_param_size(pcw, tr.vertex_type);
  }

  /* sort blended surface lists if requested */
  if (ctx->autosort) {
    tr_sort_render_list(&tr, rc, TA_LIST_TRANSLUCENT);
    tr_sort_render_list(&tr, rc, TA_LIST_PUNCH_THROUGH);
  }

#if 0
  LOG_INFO("tr_convert_convext merged %d / %d surfaces", tr.merged_surfs,
           tr.merged_surfs + rc->num_surfs);
#endif

  PROF_LEAVE();
}
