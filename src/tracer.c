#include "tracer.h"
#include "core/math.h"
#include "host.h"
#include "hw/pvr/ta.h"
#include "hw/pvr/tr.h"
#include "hw/pvr/trace.h"
#include "render/nuklear.h"

#define SCRUBBER_WINDOW_HEIGHT 20.0f

static const char *param_names[] = {
    "TA_PARAM_END_OF_LIST", "TA_PARAM_USER_TILE_CLIP", "TA_PARAM_OBJ_LIST_SET",
    "TA_PARAM_RESERVED0",   "TA_PARAM_POLY_OR_VOL",    "TA_PARAM_SPRITE",
    "TA_PARAM_RESERVED1",   "TA_PARAM_VERTEX",
};

static const char *list_names[] = {
    "TA_LIST_OPAQUE",        "TA_LIST_OPAQUE_MODVOL",
    "TA_LIST_TRANSLUCENT",   "TA_LIST_TRANSLUCENT_MODVOL",
    "TA_LIST_PUNCH_THROUGH",
};

static const char *pxl_names[] = {
    "PXL_INVALID", "PXL_RGBA",     "PXL_RGBA5551",
    "PXL_RGB565",  "PXL_RGBA4444", "PXL_RGBA8888",
};

static const char *filter_names[] = {
    "FILTER_NEAREST", "FILTER_BILINEAR",
};

static const char *wrap_names[] = {
    "WRAP_REPEAT", "WRAP_CLAMP_TO_EDGE", "WRAP_MIRRORED_REPEAT",
};

static const char *depthfunc_names[] = {
    "NONE",    "NEVER",  "LESS",   "EQUAL",  "LEQUAL",
    "GREATER", "NEQUAL", "GEQUAL", "ALWAYS",
};

static const char *cullface_names[] = {
    "NONE", "FRONT", "BACK",
};

static const char *blendfunc_names[] = {
    "NONE",
    "ZERO",
    "ONE",
    "SRC_COLOR",
    "ONE_MINUS_SRC_COLOR",
    "SRC_ALPHA",
    "ONE_MINUS_SRC_ALPHA",
    "DST_ALPHA",
    "ONE_MINUS_DST_ALPHA",
    "DST_COLOR",
    "ONE_MINUS_DST_COLOR",
};

static const char *shademode_names[] = {
    "DECAL", "MODULATE", "DECAL_ALPHA", "MODULATE_ALPHA",
};

struct tracer_texture {
  struct tr_texture;
  struct rb_node live_it;
  struct list_node free_it;
};

struct tracer {
  struct host *host;
  struct render_backend *r;
  struct nuklear *nk;
  struct tr *tr;
  struct tr_provider provider;

  /* trace state */
  struct trace *trace;
  struct tile_context ctx;
  struct trace_cmd *current_cmd;
  int current_param;
  int scroll_to_param;

  /* render state */
  struct tr_context rc;
  struct tracer_texture textures[1024];
  struct rb_tree live_textures;
  struct list free_textures;
};

static int tracer_texture_cmp(const struct rb_node *rb_lhs,
                              const struct rb_node *rb_rhs) {
  const struct tracer_texture *lhs =
      rb_entry(rb_lhs, const struct tracer_texture, live_it);
  tr_texture_key_t lhs_key = tr_texture_key(lhs->tsp, lhs->tcw);

  const struct tracer_texture *rhs =
      rb_entry(rb_rhs, const struct tracer_texture, live_it);
  tr_texture_key_t rhs_key = tr_texture_key(rhs->tsp, rhs->tcw);

  if (lhs_key < rhs_key) {
    return -1;
  } else if (lhs_key > rhs_key) {
    return 1;
  } else {
    return 0;
  }
}

static struct rb_callbacks tracer_texture_cb = {&tracer_texture_cmp, NULL,
                                                NULL};

static struct tracer_texture *tracer_find_texture(struct tracer *tracer,
                                                  union tsp tsp,
                                                  union tcw tcw) {
  struct tracer_texture search;
  search.tsp = tsp;
  search.tcw = tcw;

  return rb_find_entry(&tracer->live_textures, &search, struct tracer_texture,
                       live_it, &tracer_texture_cb);
}

static struct tr_texture *tracer_provider_find_texture(void *data,
                                                       union tsp tsp,
                                                       union tcw tcw) {
  struct tracer *tracer = data;

  struct tracer_texture *tex = tracer_find_texture(tracer, tsp, tcw);
  CHECK_NOTNULL(tex, "Texture wasn't available in cache");

  return (struct tr_texture *)tex;
}

static void tracer_add_texture(struct tracer *tracer,
                               const struct trace_cmd *cmd) {
  CHECK_EQ(cmd->type, TRACE_CMD_TEXTURE);

  struct tracer_texture *tex =
      tracer_find_texture(tracer, cmd->texture.tsp, cmd->texture.tcw);

  if (!tex) {
    tex = list_first_entry(&tracer->free_textures, struct tracer_texture,
                           free_it);
    CHECK_NOTNULL(tex);
    list_remove(&tracer->free_textures, &tex->free_it);

    tex->tsp = cmd->texture.tsp;
    tex->tcw = cmd->texture.tcw;

    rb_insert(&tracer->live_textures, &tex->live_it, &tracer_texture_cb);
  }

  tex->frame = cmd->texture.frame;
  tex->dirty = 1;
  tex->texture = cmd->texture.texture;
  tex->texture_size = cmd->texture.texture_size;
  tex->palette = cmd->texture.palette;
  tex->palette_size = cmd->texture.palette_size;
}

static void tracer_copy_context(const struct trace_cmd *cmd,
                                struct tile_context *ctx) {
  CHECK_EQ(cmd->type, TRACE_CMD_CONTEXT);

  ctx->frame = cmd->context.frame;
  ctx->autosort = cmd->context.autosort;
  ctx->stride = cmd->context.stride;
  ctx->pal_pxl_format = cmd->context.pal_pxl_format;
  ctx->bg_isp = cmd->context.bg_isp;
  ctx->bg_tsp = cmd->context.bg_tsp;
  ctx->bg_tcw = cmd->context.bg_tcw;
  ctx->bg_depth = cmd->context.bg_depth;
  ctx->video_width = cmd->context.video_width;
  ctx->video_height = cmd->context.video_height;
  memcpy(ctx->bg_vertices, cmd->context.bg_vertices,
         cmd->context.bg_vertices_size);
  memcpy(ctx->params, cmd->context.params, cmd->context.params_size);
  ctx->size = cmd->context.params_size;
}

static void tracer_prev_param(struct tracer *tracer) {
  int i = tracer->current_param;

  while (i--) {
    tracer->current_param = i;
    tracer->scroll_to_param = 1;
    break;
  }
}

static void tracer_next_param(struct tracer *tracer) {
  int i = tracer->current_param;

  while (++i < tracer->rc.num_params) {
    tracer->current_param = i;
    tracer->scroll_to_param = 1;
    break;
  }
}

static void tracer_prev_context(struct tracer *tracer) {
  struct trace_cmd *begin = tracer->current_cmd->prev;

  /* ensure that there is a prev context */
  struct trace_cmd *prev = begin;

  while (prev) {
    if (prev->type == TRACE_CMD_CONTEXT) {
      break;
    }

    prev = prev->prev;
  }

  if (!prev) {
    return;
  }

  /* walk back to the prev context, reverting any textures that've been added */
  struct trace_cmd *curr = begin;

  while (curr != prev) {
    if (curr->type == TRACE_CMD_TEXTURE) {
      struct trace_cmd * override = curr->override;

      if (override) {
        tracer_add_texture(tracer, override);
      }
    }

    curr = curr->prev;
  }

  tracer->current_cmd = prev;
  tracer->current_param = -1;
  tracer->scroll_to_param = 0;
  tracer_copy_context(tracer->current_cmd, &tracer->ctx);
  tr_parse_context(tracer->tr, &tracer->ctx, &tracer->rc);
}

static void tracer_next_context(struct tracer *tracer) {
  struct trace_cmd *begin =
      tracer->current_cmd ? tracer->current_cmd->next : tracer->trace->cmds;

  /* ensure that there is a next context */
  struct trace_cmd *next = begin;

  while (next) {
    if (next->type == TRACE_CMD_CONTEXT) {
      break;
    }

    next = next->next;
  }

  if (!next) {
    return;
  }

  /* walk towards to the next context, adding any new textures */
  struct trace_cmd *curr = begin;

  while (curr != next) {
    if (curr->type == TRACE_CMD_TEXTURE) {
      tracer_add_texture(tracer, curr);
    }

    curr = curr->next;
  }

  tracer->current_cmd = next;
  tracer->current_param = -1;
  tracer->scroll_to_param = 0;
  tracer_copy_context(tracer->current_cmd, &tracer->ctx);
  tr_parse_context(tracer->tr, &tracer->ctx, &tracer->rc);
}

static void tracer_reset_context(struct tracer *tracer) {
  tracer->current_cmd = NULL;
  tracer_next_context(tracer);
}

static void tracer_render_debug_menu(struct tracer *tracer) {
  struct nk_context *ctx = &tracer->nk->ctx;

  float fw = (float)video_width(tracer->host);
  struct nk_rect bounds = {0.0f, -1.0f, fw, DEBUG_MENU_HEIGHT + 1.0f};

  nk_style_default(ctx);
  ctx->style.window.border = 0.0f;
  ctx->style.window.menu_border = 0.0f;
  ctx->style.window.spacing = nk_vec2(0.0f, 0.0f);
  ctx->style.window.padding = nk_vec2(0.0f, 0.0f);

  if (nk_begin(ctx, "debug menu", bounds, NK_WINDOW_NO_SCROLLBAR)) {
    nk_style_default(ctx);
    ctx->style.window.padding = nk_vec2(0.0f, 0.0f);

    nk_menubar_begin(ctx);
    nk_layout_row_begin(ctx, NK_STATIC, DEBUG_MENU_HEIGHT, 1);

    {
      nk_layout_row_push(ctx, 40.0f);

      if (nk_menu_begin_label(ctx, "DEBUG", NK_TEXT_LEFT,
                              nk_vec2(140.0f, 200.0f))) {
        nk_layout_row_dynamic(ctx, DEBUG_MENU_HEIGHT, 1);

        int debug_depth = r_get_debug_flag(tracer->r, DEBUG_DEPTH_BUFFER);
        nk_checkbox_label(ctx, "depth buffer", &debug_depth);
        if (debug_depth) {
          r_set_debug_flag(tracer->r, DEBUG_DEPTH_BUFFER);
        } else {
          r_clear_debug_flag(tracer->r, DEBUG_DEPTH_BUFFER);
        }

        nk_menu_end(ctx);
      }
    }

    nk_layout_row_end(ctx);
    nk_menubar_end(ctx);
  }
  nk_end(ctx);
}

static void tracer_render_scrubber_menu(struct tracer *tracer) {
  struct nk_context *ctx = &tracer->nk->ctx;
  float width = (float)video_width(tracer->host);
  float height = (float)video_height(tracer->host);

  nk_style_default(ctx);

  /* disable spacing / padding */
  ctx->style.window.padding = nk_vec2(0.0f, 0.0f);
  ctx->style.window.spacing = nk_vec2(0.0f, 0.0f);

  struct nk_rect bounds = {0.0f, height - SCRUBBER_WINDOW_HEIGHT, width,
                           SCRUBBER_WINDOW_HEIGHT};
  nk_flags flags = NK_WINDOW_NO_SCROLLBAR;

  if (nk_begin(ctx, "context scrubber", bounds, flags)) {
    nk_layout_row_dynamic(ctx, SCRUBBER_WINDOW_HEIGHT, 1);

    nk_size frame = tracer->ctx.frame - tracer->trace->first_frame;
    nk_size max_frames = tracer->trace->last_frame - tracer->trace->first_frame;

    if (nk_progress(ctx, &frame, max_frames - 1, 1)) {
      int delta = tracer->trace->first_frame + (int)frame - tracer->ctx.frame;

      for (int i = 0; i < ABS(delta); i++) {
        if (delta > 0) {
          tracer_next_context(tracer);
        } else {
          tracer_prev_context(tracer);
        }
      }
    }
  }
  nk_end(ctx);
}

static void tracer_param_tooltip(struct tracer *tracer, struct tr_param *rp) {
  struct nk_context *ctx = &tracer->nk->ctx;

  if (nk_tooltip_begin(ctx, 300.0f)) {
    nk_layout_row_dynamic(ctx, ctx->style.font->height, 1);

    /* render source TA information */
    union pcw pcw = *(const union pcw *)(tracer->ctx.params + rp->offset);

    nk_labelf(ctx, NK_TEXT_LEFT, "pcw: 0x%x", pcw.full);
    nk_labelf(ctx, NK_TEXT_LEFT, "list type: %s", list_names[rp->list_type]);
    nk_labelf(ctx, NK_TEXT_LEFT, "surf: %d", rp->last_surf);

    if (pcw.para_type == TA_PARAM_POLY_OR_VOL ||
        pcw.para_type == TA_PARAM_SPRITE) {
      const union poly_param *param =
          (const union poly_param *)(tracer->ctx.params + rp->offset);

      nk_labelf(ctx, NK_TEXT_LEFT, "isp_tsp: 0x%x", param->type0.isp_tsp.full);
      nk_labelf(ctx, NK_TEXT_LEFT, "tsp: 0x%x", param->type0.tsp.full);
      nk_labelf(ctx, NK_TEXT_LEFT, "tcw: 0x%x", param->type0.tcw.full);

      int poly_type = ta_get_poly_type(param->type0.pcw);

      nk_labelf(ctx, NK_TEXT_LEFT, "poly type: %d", poly_type);

      switch (poly_type) {
        case 1:
          nk_labelf(ctx, NK_TEXT_LEFT, "face_color_a: %.2f",
                    param->type1.face_color_a);
          nk_labelf(ctx, NK_TEXT_LEFT, "face_color_r: %.2f",
                    param->type1.face_color_r);
          nk_labelf(ctx, NK_TEXT_LEFT, "face_color_g: %.2f",
                    param->type1.face_color_g);
          nk_labelf(ctx, NK_TEXT_LEFT, "face_color_b: %.2f",
                    param->type1.face_color_b);
          break;

        case 2:
          nk_labelf(ctx, NK_TEXT_LEFT, "face_color_a: %.2f",
                    param->type2.face_color_a);
          nk_labelf(ctx, NK_TEXT_LEFT, "face_color_r: %.2f",
                    param->type2.face_color_r);
          nk_labelf(ctx, NK_TEXT_LEFT, "face_color_g: %.2f",
                    param->type2.face_color_g);
          nk_labelf(ctx, NK_TEXT_LEFT, "face_color_b: %.2f",
                    param->type2.face_color_b);
          nk_labelf(ctx, NK_TEXT_LEFT, "face_offset_color_a: %.2f",
                    param->type2.face_offset_color_a);
          nk_labelf(ctx, NK_TEXT_LEFT, "face_offset_color_r: %.2f",
                    param->type2.face_offset_color_r);
          nk_labelf(ctx, NK_TEXT_LEFT, "face_offset_color_g: %.2f",
                    param->type2.face_offset_color_g);
          nk_labelf(ctx, NK_TEXT_LEFT, "face_offset_color_b: %.2f",
                    param->type2.face_offset_color_b);
          break;

        case 5:
          nk_labelf(ctx, NK_TEXT_LEFT, "base_color: 0x%x",
                    param->sprite.base_color);
          nk_labelf(ctx, NK_TEXT_LEFT, "offset_color: 0x%x",
                    param->sprite.offset_color);
          break;
      }
    } else if (pcw.para_type == TA_PARAM_VERTEX) {
      const union vert_param *param =
          (const union vert_param *)(tracer->ctx.params + rp->offset);

      nk_labelf(ctx, NK_TEXT_LEFT, "vert type: %d", rp->vertex_type);

      switch (rp->vertex_type) {
        case 0:
          nk_labelf(ctx, NK_TEXT_LEFT, "xyz: {%.2f, %.2f, %f}",
                    param->type0.xyz[0], param->type0.xyz[1],
                    param->type0.xyz[2]);
          nk_labelf(ctx, NK_TEXT_LEFT, "base_color: 0x%x",
                    param->type0.base_color);
          break;

        case 1:
          nk_labelf(ctx, NK_TEXT_LEFT, "xyz: {%.2f, %.2f, %f}",
                    param->type1.xyz[0], param->type1.xyz[1],
                    param->type1.xyz[2]);
          nk_labelf(ctx, NK_TEXT_LEFT, "base_color_a: %.2f",
                    param->type1.base_color_a);
          nk_labelf(ctx, NK_TEXT_LEFT, "base_color_r: %.2f",
                    param->type1.base_color_r);
          nk_labelf(ctx, NK_TEXT_LEFT, "base_color_g: %.2f",
                    param->type1.base_color_g);
          nk_labelf(ctx, NK_TEXT_LEFT, "base_color_b: %.2f",
                    param->type1.base_color_b);
          break;

        case 2:
          nk_labelf(ctx, NK_TEXT_LEFT, "xyz: {%.2f, %.2f, %f}",
                    param->type2.xyz[0], param->type2.xyz[1],
                    param->type2.xyz[2]);
          nk_labelf(ctx, NK_TEXT_LEFT, "base_intensity: %.2f",
                    param->type2.base_intensity);
          break;

        case 3:
          nk_labelf(ctx, NK_TEXT_LEFT, "xyz: {%.2f, %.2f, %f}",
                    param->type3.xyz[0], param->type3.xyz[1],
                    param->type3.xyz[2]);
          nk_labelf(ctx, NK_TEXT_LEFT, "uv: {%.2f, %.2f}", param->type3.uv[0],
                    param->type3.uv[1]);
          nk_labelf(ctx, NK_TEXT_LEFT, "base_color: 0x%x",
                    param->type3.base_color);
          nk_labelf(ctx, NK_TEXT_LEFT, "offset_color: 0x%x",
                    param->type3.offset_color);
          break;

        case 4:
          nk_labelf(ctx, NK_TEXT_LEFT, "xyz: {0x%x, 0x%x, 0x%x}",
                    *(uint32_t *)(float *)&param->type4.xyz[0],
                    *(uint32_t *)(float *)&param->type4.xyz[1],
                    *(uint32_t *)(float *)&param->type4.xyz[2]);
          nk_labelf(ctx, NK_TEXT_LEFT, "uv: {0x%x, 0x%x}", param->type4.vu[1],
                    param->type4.vu[0]);
          nk_labelf(ctx, NK_TEXT_LEFT, "base_color: 0x%x",
                    param->type4.base_color);
          nk_labelf(ctx, NK_TEXT_LEFT, "offset_color: 0x%x",
                    param->type4.offset_color);
          break;

        case 5:
          nk_labelf(ctx, NK_TEXT_LEFT, "xyz: {%.2f, %.2f, %f}",
                    param->type5.xyz[0], param->type5.xyz[1],
                    param->type5.xyz[2]);
          nk_labelf(ctx, NK_TEXT_LEFT, "uv: {%.2f, %.2f}", param->type5.uv[0],
                    param->type5.uv[1]);
          nk_labelf(ctx, NK_TEXT_LEFT, "base_color_a: %.2f",
                    param->type5.base_color_a);
          nk_labelf(ctx, NK_TEXT_LEFT, "base_color_r: %.2f",
                    param->type5.base_color_r);
          nk_labelf(ctx, NK_TEXT_LEFT, "base_color_g: %.2f",
                    param->type5.base_color_g);
          nk_labelf(ctx, NK_TEXT_LEFT, "base_color_b: %.2f",
                    param->type5.base_color_b);
          nk_labelf(ctx, NK_TEXT_LEFT, "offset_color_a: %.2f",
                    param->type5.offset_color_a);
          nk_labelf(ctx, NK_TEXT_LEFT, "offset_color_r: %.2f",
                    param->type5.offset_color_r);
          nk_labelf(ctx, NK_TEXT_LEFT, "offset_color_g: %.2f",
                    param->type5.offset_color_g);
          nk_labelf(ctx, NK_TEXT_LEFT, "offset_color_b: %.2f",
                    param->type5.offset_color_b);
          break;

        case 6:
          nk_labelf(ctx, NK_TEXT_LEFT, "xyz: {%.2f, %.2f, %f}",
                    param->type6.xyz[0], param->type6.xyz[1],
                    param->type6.xyz[2]);
          nk_labelf(ctx, NK_TEXT_LEFT, "uv: {0x%x, 0x%x}", param->type6.vu[1],
                    param->type6.vu[0]);
          nk_labelf(ctx, NK_TEXT_LEFT, "base_color_a: %.2f",
                    param->type6.base_color_a);
          nk_labelf(ctx, NK_TEXT_LEFT, "base_color_r: %.2f",
                    param->type6.base_color_r);
          nk_labelf(ctx, NK_TEXT_LEFT, "base_color_g: %.2f",
                    param->type6.base_color_g);
          nk_labelf(ctx, NK_TEXT_LEFT, "base_color_b: %.2f",
                    param->type6.base_color_b);
          nk_labelf(ctx, NK_TEXT_LEFT, "offset_color_a: %.2f",
                    param->type6.offset_color_a);
          nk_labelf(ctx, NK_TEXT_LEFT, "offset_color_r: %.2f",
                    param->type6.offset_color_r);
          nk_labelf(ctx, NK_TEXT_LEFT, "offset_color_g: %.2f",
                    param->type6.offset_color_g);
          nk_labelf(ctx, NK_TEXT_LEFT, "offset_color_b: %.2f",
                    param->type6.offset_color_b);
          break;

        case 7:
          nk_labelf(ctx, NK_TEXT_LEFT, "xyz: {%.2f, %.2f, %f}",
                    param->type7.xyz[0], param->type7.xyz[1],
                    param->type7.xyz[2]);
          nk_labelf(ctx, NK_TEXT_LEFT, "uv: {%.2f, %.2f}", param->type7.uv[0],
                    param->type7.uv[1]);
          nk_labelf(ctx, NK_TEXT_LEFT, "base_intensity: %.2f",
                    param->type7.base_intensity);
          nk_labelf(ctx, NK_TEXT_LEFT, "offset_intensity: %.2f",
                    param->type7.offset_intensity);
          break;

        case 8:
          nk_labelf(ctx, NK_TEXT_LEFT, "xyz: {%.2f, %.2f, %f}",
                    param->type8.xyz[0], param->type8.xyz[1],
                    param->type8.xyz[2]);
          nk_labelf(ctx, NK_TEXT_LEFT, "uv: {0x%x, 0x%x}", param->type8.vu[1],
                    param->type8.vu[0]);
          nk_labelf(ctx, NK_TEXT_LEFT, "base_intensity: %.2f",
                    param->type8.base_intensity);
          nk_labelf(ctx, NK_TEXT_LEFT, "offset_intensity: %.2f",
                    param->type8.offset_intensity);
          break;
      }
    }

    /* always render translated surface information. new surfaces can be created
       without receiving a new TA_PARAM_POLY_OR_VOL / TA_PARAM_SPRITE */
    if (rp->last_surf >= 0) {
      struct ta_surface *surf = &tracer->rc.surfs[rp->last_surf];

      /* TODO separator */

      if (surf->texture) {
        nk_layout_row_static(ctx, 40.0f, 40, 1);
        nk_image(ctx, nk_image_id((int)surf->texture));
      }

      nk_layout_row_dynamic(ctx, ctx->style.font->height, 1);
      nk_labelf(ctx, NK_TEXT_LEFT, "depth_write: %d", surf->depth_write);
      nk_labelf(ctx, NK_TEXT_LEFT, "depth_func: %s",
                depthfunc_names[surf->depth_func]);
      nk_labelf(ctx, NK_TEXT_LEFT, "cull: %s", cullface_names[surf->cull]);
      nk_labelf(ctx, NK_TEXT_LEFT, "src_blend: %s",
                blendfunc_names[surf->src_blend]);
      nk_labelf(ctx, NK_TEXT_LEFT, "dst_blend: %s",
                blendfunc_names[surf->dst_blend]);
      nk_labelf(ctx, NK_TEXT_LEFT, "shade: %s", shademode_names[surf->shade]);
      nk_labelf(ctx, NK_TEXT_LEFT, "ignore_alpha: %d", surf->ignore_alpha);
      nk_labelf(ctx, NK_TEXT_LEFT, "ignore_texture_alpha: %d",
                surf->ignore_texture_alpha);
      nk_labelf(ctx, NK_TEXT_LEFT, "offset_color: %d", surf->offset_color);
      nk_labelf(ctx, NK_TEXT_LEFT, "first_vert: %d", surf->first_vert);
      nk_labelf(ctx, NK_TEXT_LEFT, "num_verts: %d", surf->num_verts);
    }

    /* render translated vert only when rendering a vertex tooltip */
    if (rp->last_vert >= 0) {
      struct ta_vertex *vert = &tracer->rc.verts[rp->last_vert];

      /* TODO separator */

      nk_labelf(ctx, NK_TEXT_LEFT, "vert: %d", rp->last_vert);
      nk_labelf(ctx, NK_TEXT_LEFT, "xyz: {%.2f, %.2f, %f}", vert->xyz[0],
                vert->xyz[1], vert->xyz[2]);
      nk_labelf(ctx, NK_TEXT_LEFT, "uv: {%.2f, %.2f}", vert->uv[0],
                vert->uv[1]);
      nk_labelf(ctx, NK_TEXT_LEFT, "color: 0x%08x", vert->color);
      nk_labelf(ctx, NK_TEXT_LEFT, "offset_color: 0x%08x", vert->offset_color);
    }

    nk_tooltip_end(ctx);
  }
}

static void tracer_render_side_menu(struct tracer *tracer) {
  struct nk_context *ctx = &tracer->nk->ctx;
  float width = (float)video_width(tracer->host);
  float height = (float)video_height(tracer->host);

  /* transparent menu backgrounds / selectables */

  {
    struct nk_rect bounds = {
        0.0f, DEBUG_MENU_HEIGHT, 240.0f,
        height - DEBUG_MENU_HEIGHT - SCRUBBER_WINDOW_HEIGHT};

    nk_style_default(ctx);

    ctx->style.window.fixed_background.data.color.a = 128;
    ctx->style.selectable.normal.data.color.a = 0;
    ctx->style.window.padding = nk_vec2(0.0f, 0.0f);

    if (nk_begin(ctx, "params", bounds, NK_WINDOW_MINIMIZABLE |
                                            NK_WINDOW_NO_SCROLLBAR |
                                            NK_WINDOW_TITLE)) {
      /* fill entire panel */
      struct nk_vec2 region = nk_window_get_content_region_size(ctx);
      nk_layout_row_dynamic(ctx, region.y, 1);

      /* "disable" backgrounds for children elements to avoid blending
         with the partially transparent parent panel */
      ctx->style.window.fixed_background.data.color.a = 0;

      struct nk_list_view view;
      int param_height = 15;
      int num_params = tracer->rc.num_params;
      char label[128];

      if (nk_list_view_begin(ctx, &view, "params list", 0, param_height,
                             num_params)) {
        nk_layout_row_dynamic(ctx, (float)param_height, 1);

        for (int i = view.begin; i < view.end && i < num_params; i++) {
          struct tr_param *rp = &tracer->rc.params[i];
          union pcw pcw = *(const union pcw *)(tracer->ctx.params + rp->offset);

          int selected = (i == tracer->current_param);

          struct nk_rect bounds = nk_widget_bounds(ctx);
          snprintf(label, sizeof(label), "0x%04x %s", rp->offset,
                   param_names[pcw.para_type]);
          nk_selectable_label(ctx, label, NK_TEXT_LEFT, &selected);

          switch (pcw.para_type) {
            case TA_PARAM_POLY_OR_VOL:
            case TA_PARAM_SPRITE:
              if (nk_input_is_mouse_hovering_rect(&ctx->input, bounds)) {
                tracer_param_tooltip(tracer, rp);
              }
              break;

            case TA_PARAM_VERTEX:
              if (nk_input_is_mouse_hovering_rect(&ctx->input, bounds)) {
                tracer_param_tooltip(tracer, rp);
              }
              break;
          }

          if (selected) {
            tracer->current_param = i;
          }
        }

        /* scroll to parameter if not visible */
        if (tracer->scroll_to_param) {
          struct nk_window *win = ctx->current;
          struct nk_panel *layout = win->layout;

          if (tracer->current_param < view.begin) {
            *layout->offset_y -= (nk_uint)layout->bounds.h;
          } else if (tracer->current_param >= view.end) {
            *layout->offset_y += (nk_uint)layout->bounds.h;
          }

          tracer->scroll_to_param = 0;
        }

        nk_list_view_end(&view);
      }
    }

    nk_end(ctx);
  }

  {
    struct nk_rect bounds = {
        width - 240.0f, DEBUG_MENU_HEIGHT, 240.0f,
        height - DEBUG_MENU_HEIGHT - SCRUBBER_WINDOW_HEIGHT};

    nk_style_default(ctx);

    ctx->style.window.fixed_background.data.color.a = 128;

    if (nk_begin(ctx, "textures", bounds,
                 NK_WINDOW_MINIMIZABLE | NK_WINDOW_TITLE)) {
      nk_layout_row_static(ctx, 40.0f, 40, 5);

      rb_for_each_entry(tex, &tracer->live_textures, struct tracer_texture,
                        live_it) {
        struct nk_rect bounds = nk_widget_bounds(ctx);

        nk_image(ctx, nk_image_id((int)tex->handle));

        if (nk_input_is_mouse_hovering_rect(&ctx->input, bounds)) {
          /* disable spacing for tooltip */
          struct nk_vec2 original_spacing = ctx->style.window.spacing;
          ctx->style.window.spacing = nk_vec2(0.0f, 0.0f);

          if (nk_tooltip_begin(ctx, 184.0f)) {
            nk_layout_row_static(ctx, 184.0f, 184, 1);
            nk_image(ctx, nk_image_id((int)tex->handle));

            nk_layout_row_dynamic(ctx, ctx->style.font->height, 1);
            nk_labelf(ctx, NK_TEXT_LEFT, "addr: 0x%08x",
                      ta_texture_addr(tex->tcw));
            nk_labelf(ctx, NK_TEXT_LEFT, "format: %s", pxl_names[tex->format]);
            nk_labelf(ctx, NK_TEXT_LEFT, "filter: %s",
                      filter_names[tex->filter]);
            nk_labelf(ctx, NK_TEXT_LEFT, "wrap_u: %s", wrap_names[tex->wrap_u]);
            nk_labelf(ctx, NK_TEXT_LEFT, "wrap_v: %s", wrap_names[tex->wrap_v]);
            nk_labelf(ctx, NK_TEXT_LEFT, "twiddled: %d",
                      ta_texture_twiddled(tex->tcw));
            nk_labelf(ctx, NK_TEXT_LEFT, "compressed: %d",
                      ta_texture_compressed(tex->tcw));
            nk_labelf(ctx, NK_TEXT_LEFT, "mipmaps: %d",
                      ta_texture_mipmaps(tex->tcw));
            nk_labelf(ctx, NK_TEXT_LEFT, "width: %d", tex->width);
            nk_labelf(ctx, NK_TEXT_LEFT, "height: %d", tex->height);
            nk_labelf(ctx, NK_TEXT_LEFT, "texture_size: %d", tex->texture_size);

            nk_tooltip_end(ctx);
          }

          /* restore spacing */
          ctx->style.window.spacing = original_spacing;
        }
      }
    }

    nk_end(ctx);
  }
}

static void tracer_render_list(struct tracer *tracer,
                               const struct tr_context *rc, int list_type,
                               int end, int *stopped) {
  if (*stopped) {
    return;
  }

  const struct tr_list *list = &tracer->rc.lists[list_type];
  const int *sorted_surf = list->surfs;
  const int *sorted_surf_end = list->surfs + list->num_surfs;

  while (sorted_surf < sorted_surf_end) {
    int idx = *(sorted_surf++);

    r_draw_ta_surface(tracer->r, &tracer->rc.surfs[idx]);

    if (idx == end) {
      *stopped = 1;
      break;
    }
  }
}

static void tracer_input_mousemove(void *data, int port, int x, int y) {
  struct tracer *tracer = data;

  nk_mousemove(tracer->nk, x, y);
}

static void tracer_input_keydown(void *data, int port, enum keycode key,
                                 int16_t value) {
  struct tracer *tracer = data;

  if (key == K_LEFT && value > 0) {
    tracer_prev_context(tracer);
  } else if (key == K_RIGHT && value > 0) {
    tracer_next_context(tracer);
  } else if (key == K_UP && value > 0) {
    tracer_prev_param(tracer);
  } else if (key == K_DOWN && value > 0) {
    tracer_next_param(tracer);
  } else {
    nk_keydown(tracer->nk, key, value);
  }
}

void tracer_run_frame(struct tracer *tracer) {
  int width = video_width(tracer->host);
  int height = video_height(tracer->host);

  r_clear_viewport(tracer->r, width, height);

  nk_update_input(tracer->nk);

  /* render ui */
  tracer_render_side_menu(tracer);
  tracer_render_scrubber_menu(tracer);
  tracer_render_debug_menu(tracer);

  /* render context up to the surface of the currently selected param */
  {
    struct tr_context *rc = &tracer->rc;
    int stopped = 0;
    int end = -1;

    if (tracer->current_param >= 0) {
      struct tr_param *rp = &rc->params[tracer->current_param];
      end = rp->last_surf;
    }

    r_begin_ta_surfaces(tracer->r, rc->projection, rc->verts, rc->num_verts);

    tracer_render_list(tracer, rc, TA_LIST_OPAQUE, end, &stopped);
    tracer_render_list(tracer, rc, TA_LIST_PUNCH_THROUGH, end, &stopped);
    tracer_render_list(tracer, rc, TA_LIST_TRANSLUCENT, end, &stopped);

    r_end_ta_surfaces(tracer->r);
  }

  nk_render(tracer->nk);
}

int tracer_load(struct tracer *tracer, const char *path) {
  if (tracer->trace) {
    trace_destroy(tracer->trace);
    tracer->trace = NULL;
  }

  tracer->trace = trace_parse(path);

  if (!tracer->trace) {
    LOG_WARNING("Failed to parse %s", path);
    return 0;
  }

  tracer_reset_context(tracer);

  return 1;
}

void tracer_destroy(struct tracer *tracer) {
  if (tracer->trace) {
    trace_destroy(tracer->trace);
  }

  tr_destroy(tracer->tr);
  nk_destroy(tracer->nk);
  r_destroy(tracer->r);

  free(tracer);
}

struct tracer *tracer_create(struct host *host) {
  struct tracer *tracer = calloc(1, sizeof(struct tracer));

  /* setup host, bind event callbacks */
  tracer->host = host;
  tracer->host->userdata = tracer;
  tracer->host->input_keydown = &tracer_input_keydown;
  tracer->host->input_mousemove = &tracer_input_mousemove;

  /* setup renderer */
  tracer->provider.userdata = tracer;
  tracer->provider.find_texture = &tracer_provider_find_texture;
  tracer->r = r_create(tracer->host);
  tracer->nk = nk_create(tracer->r);
  tracer->tr = tr_create(tracer->r, &tracer->provider);

  /* add all textures to free list */
  for (int i = 0, n = array_size(tracer->textures); i < n; i++) {
    struct tracer_texture *tex = &tracer->textures[i];
    list_add(&tracer->free_textures, &tex->free_it);
  }

  return tracer;
}
