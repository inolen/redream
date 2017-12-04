#include "tracer.h"
#include "core/core.h"
#include "core/rb_tree.h"
#include "file/trace.h"
#include "guest/pvr/ta.h"
#include "guest/pvr/tr.h"
#include "host/host.h"
#include "imgui.h"

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

static const char *pixel_names[] = {
    "TA_PXL_1555",    "TA_PXL_565",  "TA_PXL_4444", "TA_PXL_YUV422",
    "TA_PXL_BUMPMAP", "TA_PXL_4BPP", "TA_PXL_8BPP", "TA_PXL_RESERVED",
};

static const char *palette_names[] = {
    "TA_PAL_ARGB1555", "TA_PAL_RGB565", "TA_PAL_ARGB4444", "TA_PAL_ARGB8888",
};

static const char *texture_fmt_names[] = {
    "PVR_TEX_INVALID",
    "PVR_TEX_TWIDDLED",
    "PVR_TEX_TWIDDLED_MIPMAPS",
    "PVR_TEX_VQ",
    "PVR_TEX_VQ_MIPMAPS",
    "PVR_TEX_PALETTE_4BPP",
    "PVR_TEX_PALETTE_4BPP_MIPMAPS",
    "PVR_TEX_PALETTE_8BPP",
    "PVR_TEX_PALETTE_8BPP_MIPMAPS",
    "PVR_TEX_BITMAP_RECT",
    NULL,
    "PVR_TEX_BITMAP",
    NULL,
    "PVR_TEX_TWIDDLED_RECT",
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

static const struct ImVec2 zero_vec2 = {0.0f, 0.0f};
static const struct ImVec4 one_vec4 = {1.0f, 1.0f, 1.0f, 1.0f};
static const struct ImVec4 zero_vec4 = {0.0f, 0.0f, 0.0f, 0.0f};

struct tracer_texture {
  struct tr_texture;
  struct rb_node live_it;
  struct list_node free_it;
};

struct tracer {
  struct host *host;
  struct render_backend *r;

  /* trace state */
  struct trace *trace;
  struct ta_context ctx;
  struct trace_cmd *current_cmd;
  int frame;
  int current_param;
  int scroll_to_param;

  /* render state */
  struct tr_context rc;
  int debug_depth;
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

static void tracer_free_texture(struct tracer *tracer,
                                struct tracer_texture *tex) {
  /* remove from live tree */
  rb_unlink(&tracer->live_textures, &tex->live_it, &tracer_texture_cb);

  /* add back to free list */
  list_add(&tracer->free_textures, &tex->free_it);
}

static struct tracer_texture *tracer_alloc_texture(struct tracer *tracer,
                                                   union tsp tsp,
                                                   union tcw tcw) {
  /* remove from free list */
  struct tracer_texture *tex =
      list_first_entry(&tracer->free_textures, struct tracer_texture, free_it);
  CHECK_NOTNULL(tex);
  list_remove(&tracer->free_textures, &tex->free_it);

  /* reset tex */
  memset(tex, 0, sizeof(*tex));
  tex->tsp = tsp;
  tex->tcw = tcw;

  /* add to live tree */
  rb_insert(&tracer->live_textures, &tex->live_it, &tracer_texture_cb);

  return tex;
}
static struct tr_texture *tracer_find_texture(void *userdata, union tsp tsp,
                                              union tcw tcw) {
  struct tracer *tracer = userdata;

  struct tracer_texture search;
  search.tsp = tsp;
  search.tcw = tcw;

  struct tracer_texture *tex =
      rb_find_entry(&tracer->live_textures, &search, struct tracer_texture,
                    live_it, &tracer_texture_cb);
  return (struct tr_texture *)tex;
}

static void tracer_add_texture(struct tracer *tracer,
                               const struct trace_cmd *cmd) {
  CHECK_EQ(cmd->type, TRACE_CMD_TEXTURE);

  struct tracer_texture *tex = (struct tracer_texture *)tracer_find_texture(
      tracer, cmd->texture.tsp, cmd->texture.tcw);

  if (!tex) {
    tex = tracer_alloc_texture(tracer, cmd->texture.tsp, cmd->texture.tcw);
  }

  tex->frame = cmd->texture.frame;
  tex->dirty = 1;
  tex->texture = cmd->texture.texture;
  tex->texture_size = cmd->texture.texture_size;
  tex->palette = cmd->texture.palette;
  tex->palette_size = cmd->texture.palette_size;
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
      struct trace_cmd *override = curr->override;

      if (override) {
        tracer_add_texture(tracer, override);
      }
    }

    curr = curr->prev;
  }

  tracer->frame = MAX(tracer->frame - 1, 0);
  tracer->current_cmd = prev;
  tracer->current_param = -1;
  tracer->scroll_to_param = 0;
  trace_copy_context(tracer->current_cmd, &tracer->ctx);
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

  tracer->frame = MIN(tracer->frame + 1, tracer->trace->num_frames - 1);
  tracer->current_cmd = next;
  tracer->current_param = -1;
  tracer->scroll_to_param = 0;
  trace_copy_context(tracer->current_cmd, &tracer->ctx);
}

static void tracer_reset_context(struct tracer *tracer) {
  tracer->current_cmd = NULL;
  tracer_next_context(tracer);
}

static void tracer_render_debug_menu(struct tracer *tracer) {
  if (igBeginMainMenuBar()) {
    if (igBeginMenu("DEBUG", 1)) {
      if (igMenuItem("depth buffer", NULL, tracer->debug_depth, 1)) {
        tracer->debug_depth = !tracer->debug_depth;
      }

      igEndMenu();
    }

    igEndMainMenuBar();
  }
}

static void tracer_render_scrubber_menu(struct tracer *tracer) {
  struct ImGuiIO *io = igGetIO();

  igPushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
  igBegin("scrubber", NULL,
          ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
              ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar);

  struct ImVec2 size = {io->DisplaySize.x, 34.0f};
  struct ImVec2 pos = {0.0f, io->DisplaySize.y - 34.0f};
  igSetWindowSize(size, 0);
  igSetWindowPos(pos, 0);
  igPushItemWidth(-1.0f);

  int frame = tracer->frame;
  int num_frames = tracer->trace->num_frames;

  if (igSliderInt("", &frame, 0, num_frames - 1, NULL)) {
    while (tracer->frame != (int)frame) {
      if (tracer->frame < (int)frame) {
        tracer_next_context(tracer);
      } else {
        tracer_prev_context(tracer);
      }
    }
  }

  igPopItemWidth();
  igEnd();
  igPopStyleVar(1);
}

static void tracer_param_tooltip(struct tracer *tracer, struct tr_param *rp) {
  igBeginTooltip();

  /* render source TA information */
  union pcw pcw = *(const union pcw *)(tracer->ctx.params + rp->offset);

  igText("pcw: 0x%x", pcw.full);
  igText("list type: %s", list_names[rp->list_type]);
  igText("surf: %d", rp->last_surf);

  if (pcw.para_type == TA_PARAM_POLY_OR_VOL ||
      pcw.para_type == TA_PARAM_SPRITE) {
    const union poly_param *param =
        (const union poly_param *)(tracer->ctx.params + rp->offset);

    igText("isp: 0x%x", param->type0.isp.full);
    igText("tsp: 0x%x", param->type0.tsp.full);
    igText("tcw: 0x%x", param->type0.tcw.full);

    int poly_type = ta_poly_type(param->type0.pcw);

    igText("poly type: %d", poly_type);

    switch (poly_type) {
      case 1:
        igText("face_color_a: %.2f", param->type1.face_color_a);
        igText("face_color_r: %.2f", param->type1.face_color_r);
        igText("face_color_g: %.2f", param->type1.face_color_g);
        igText("face_color_b: %.2f", param->type1.face_color_b);
        break;

      case 2:
        igText("face_color_a: %.2f", param->type2.face_color_a);
        igText("face_color_r: %.2f", param->type2.face_color_r);
        igText("face_color_g: %.2f", param->type2.face_color_g);
        igText("face_color_b: %.2f", param->type2.face_color_b);
        igText("face_offset_color_a: %.2f", param->type2.face_offset_color_a);
        igText("face_offset_color_r: %.2f", param->type2.face_offset_color_r);
        igText("face_offset_color_g: %.2f", param->type2.face_offset_color_g);
        igText("face_offset_color_b: %.2f", param->type2.face_offset_color_b);
        break;

      case 5:
        igText("base_color: 0x%x", param->sprite.base_color);
        igText("offset_color: 0x%x", param->sprite.offset_color);
        break;
    }
  } else if (pcw.para_type == TA_PARAM_VERTEX) {
    const union vert_param *param =
        (const union vert_param *)(tracer->ctx.params + rp->offset);

    igText("vert type: %d", rp->vert_type);

    switch (rp->vert_type) {
      case 0:
        igText("xyz: {%.2f, %.2f, %f}", param->type0.xyz[0],
               param->type0.xyz[1], param->type0.xyz[2]);
        igText("base_color: 0x%x", param->type0.base_color);
        break;

      case 1:
        igText("xyz: {%.2f, %.2f, %f}", param->type1.xyz[0],
               param->type1.xyz[1], param->type1.xyz[2]);
        igText("base_color_a: %.2f", param->type1.base_color_a);
        igText("base_color_r: %.2f", param->type1.base_color_r);
        igText("base_color_g: %.2f", param->type1.base_color_g);
        igText("base_color_b: %.2f", param->type1.base_color_b);
        break;

      case 2:
        igText("xyz: {%.2f, %.2f, %f}", param->type2.xyz[0],
               param->type2.xyz[1], param->type2.xyz[2]);
        igText("base_intensity: %.2f", param->type2.base_intensity);
        break;

      case 3:
        igText("xyz: {%.2f, %.2f, %f}", param->type3.xyz[0],
               param->type3.xyz[1], param->type3.xyz[2]);
        igText("uv: {%.2f, %.2f}", param->type3.uv[0], param->type3.uv[1]);
        igText("base_color: 0x%x", param->type3.base_color);
        igText("offset_color: 0x%x", param->type3.offset_color);
        break;

      case 4:
        igText("xyz: {0x%x, 0x%x, 0x%x}",
               *(uint32_t *)(float *)&param->type4.xyz[0],
               *(uint32_t *)(float *)&param->type4.xyz[1],
               *(uint32_t *)(float *)&param->type4.xyz[2]);
        igText("uv: {0x%x, 0x%x}", param->type4.uv[1], param->type4.uv[0]);
        igText("base_color: 0x%x", param->type4.base_color);
        igText("offset_color: 0x%x", param->type4.offset_color);
        break;

      case 5:
        igText("xyz: {%.2f, %.2f, %f}", param->type5.xyz[0],
               param->type5.xyz[1], param->type5.xyz[2]);
        igText("uv: {%.2f, %.2f}", param->type5.uv[0], param->type5.uv[1]);
        igText("base_color_a: %.2f", param->type5.base_color_a);
        igText("base_color_r: %.2f", param->type5.base_color_r);
        igText("base_color_g: %.2f", param->type5.base_color_g);
        igText("base_color_b: %.2f", param->type5.base_color_b);
        igText("offset_color_a: %.2f", param->type5.offset_color_a);
        igText("offset_color_r: %.2f", param->type5.offset_color_r);
        igText("offset_color_g: %.2f", param->type5.offset_color_g);
        igText("offset_color_b: %.2f", param->type5.offset_color_b);
        break;

      case 6:
        igText("xyz: {%.2f, %.2f, %f}", param->type6.xyz[0],
               param->type6.xyz[1], param->type6.xyz[2]);
        igText("uv: {0x%x, 0x%x}", param->type6.uv[1], param->type6.uv[0]);
        igText("base_color_a: %.2f", param->type6.base_color_a);
        igText("base_color_r: %.2f", param->type6.base_color_r);
        igText("base_color_g: %.2f", param->type6.base_color_g);
        igText("base_color_b: %.2f", param->type6.base_color_b);
        igText("offset_color_a: %.2f", param->type6.offset_color_a);
        igText("offset_color_r: %.2f", param->type6.offset_color_r);
        igText("offset_color_g: %.2f", param->type6.offset_color_g);
        igText("offset_color_b: %.2f", param->type6.offset_color_b);
        break;

      case 7:
        igText("xyz: {%.2f, %.2f, %f}", param->type7.xyz[0],
               param->type7.xyz[1], param->type7.xyz[2]);
        igText("uv: {%.2f, %.2f}", param->type7.uv[0], param->type7.uv[1]);
        igText("base_intensity: %.2f", param->type7.base_intensity);
        igText("offset_intensity: %.2f", param->type7.offset_intensity);
        break;

      case 8:
        igText("xyz: {%.2f, %.2f, %f}", param->type8.xyz[0],
               param->type8.xyz[1], param->type8.xyz[2]);
        igText("uv: {0x%x, 0x%x}", param->type8.uv[1], param->type8.uv[0]);
        igText("base_intensity: %.2f", param->type8.base_intensity);
        igText("offset_intensity: %.2f", param->type8.offset_intensity);
        break;
    }
  }

  /* always render translated surface information. new surfaces can be created
     without receiving a new TA_PARAM_POLY_OR_VOL / TA_PARAM_SPRITE */
  if (rp->last_surf >= 0) {
    struct ta_surface *surf = &tracer->rc.surfs[rp->last_surf];

    igSeparator();

    if (surf->params.texture) {
      struct ImVec2 tex_size = {128.0f, 128.0f};
      struct ImVec2 tex_uv0 = {0.0f, 1.0f};
      struct ImVec2 tex_uv1 = {1.0f, 0.0f};

      ImTextureID handle_id = (ImTextureID)(intptr_t)surf->params.texture;
      igImage(handle_id, tex_size, tex_uv0, tex_uv1, one_vec4, zero_vec4);
    }

    igText("depth_write: %d", surf->params.depth_write);
    igText("depth_func: %s", depthfunc_names[surf->params.depth_func]);
    igText("cull: %s", cullface_names[surf->params.cull]);
    igText("src_blend: %s", blendfunc_names[surf->params.src_blend]);
    igText("dst_blend: %s", blendfunc_names[surf->params.dst_blend]);
    igText("shade: %s", shademode_names[surf->params.shade]);
    igText("ignore_alpha: %d", surf->params.ignore_alpha);
    igText("ignore_texture_alpha: %d", surf->params.ignore_texture_alpha);
    igText("offset_color: %d", surf->params.offset_color);
    igText("first_vert: %d", surf->first_vert);
    igText("num_verts: %d", surf->num_verts);
  }

  /* render translated vert only when rendering a vertex tooltip */
  if (rp->last_vert >= 0) {
    struct ta_vertex *vert = &tracer->rc.verts[rp->last_vert];

    igSeparator();

    igText("vert: %d", rp->last_vert);
    igText("xyz: {%.2f, %.2f, %f}", vert->xyz[0], vert->xyz[1], vert->xyz[2]);
    igText("uv: {%.2f, %.2f}", vert->uv[0], vert->uv[1]);
    igText("color: 0x%08x", vert->color);
    igText("offset_color: 0x%08x", vert->offset_color);
  }

  igEndTooltip();
}

static void tracer_render_side_menu(struct tracer *tracer) {
  struct ImGuiIO *io = igGetIO();

  char label[128];

  /* context params */
  if (igBegin("params", NULL, 0)) {
    struct ImVec2 size = {220.0f, io->DisplaySize.y * 0.85f};
    struct ImVec2 pos = {0.0f, io->DisplaySize.y * 0.05f};
    igSetWindowSize(size, ImGuiCond_Once);
    igSetWindowPos(pos, ImGuiCond_Once);

    /* render params */
    for (int i = 0; i < tracer->rc.num_params; i++) {
      struct tr_param *rp = &tracer->rc.params[i];
      union pcw pcw = *(const union pcw *)(tracer->ctx.params + rp->offset);

      int selected = (i == tracer->current_param);
      snprintf(label, sizeof(label), "0x%04x %s", rp->offset,
               param_names[pcw.para_type]);

      if (igSelectable(label, selected, 0, zero_vec2)) {
        selected = !selected;
      }

      switch (pcw.para_type) {
        case TA_PARAM_POLY_OR_VOL:
        case TA_PARAM_SPRITE:
          if (igIsItemHovered()) {
            tracer_param_tooltip(tracer, rp);
          }
          break;

        case TA_PARAM_VERTEX:
          if (igIsItemHovered()) {
            tracer_param_tooltip(tracer, rp);
          }
          break;
      }

      if (selected) {
        tracer->current_param = i;

        if (tracer->scroll_to_param) {
          if (igIsItemVisible()) {
            igSetScrollHere(0.5f);
          }

          tracer->scroll_to_param = 0;
        }
      }
    }

    igEnd();
  }

  /* texture window */
  if (igBegin("textures", NULL, 0)) {
    struct ImVec2 size = {220.0f, io->DisplaySize.y * 0.85f * 0.5f};
    struct ImVec2 pos = {io->DisplaySize.x - 220.0f, io->DisplaySize.y * 0.05f};
    igSetWindowSize(size, ImGuiCond_Once);
    igSetWindowPos(pos, ImGuiCond_Once);

    int i = 0;
    int tex_per_row = MAX((int)(igGetContentRegionAvailWidth() / 44.0f), 1);

    rb_for_each_entry(tex, &tracer->live_textures, struct tracer_texture,
                      live_it) {
      ImTextureID handle_id = (ImTextureID)(intptr_t)tex->handle;

      {
        struct ImVec2 tex_size = {32.0f, 32.0f};
        struct ImVec2 tex_uv0 = {0.0f, 1.0f};
        struct ImVec2 tex_uv1 = {1.0f, 0.0f};

        igPushStyleColor(ImGuiCol_Button, zero_vec4);
        igImageButton(handle_id, tex_size, tex_uv0, tex_uv1, -1, one_vec4,
                      one_vec4);
        igPopStyleColor(1);
      }

      {
        char popup_name[128];
        snprintf(popup_name, sizeof(popup_name), "texture_%d", tex->handle);

        if (igBeginPopupContextItem(popup_name, 0)) {
          struct ImVec2 tex_size = {128.0f, 128.0f};
          struct ImVec2 tex_uv0 = {0.0f, 1.0f};
          struct ImVec2 tex_uv1 = {1.0f, 0.0f};

          igImage(handle_id, tex_size, tex_uv0, tex_uv1, one_vec4, zero_vec4);
          igSeparator();
          igText("addr; 0x%08x", tex->tcw.texture_addr << 3);
          igText("texture_fmt: %s", texture_fmt_names[tex->format]);
          igText("pixel_fmt: %s", pixel_names[tex->tcw.pixel_fmt]);
          igText("palette_fmt: %s", palette_names[tracer->ctx.palette_fmt]);
          igText("filter: %s", filter_names[tex->filter]);
          igText("wrap_u: %s", wrap_names[tex->wrap_u]);
          igText("wrap_v: %s", wrap_names[tex->wrap_v]);
          igText("width: %d", tex->width);
          igText("height: %d", tex->height);

          igEndPopup();
        }
      }

      if ((++i % tex_per_row) != 0) {
        igSameLine(0.0f, -1.0f);
      }
    }

    igEnd();
  }

  if (igBegin("debug info", NULL, 0)) {
    struct ImVec2 size = {220.0f, io->DisplaySize.y * 0.85f * 0.5f};
    struct ImVec2 pos = {io->DisplaySize.x - 220.0f,
                         io->DisplaySize.y * 0.05f + size.y};
    igSetWindowSize(size, ImGuiCond_Once);
    igSetWindowPos(pos, ImGuiCond_Once);

    int total_orig_surfs = 0;
    int total_surfs = 0;

    for (int i = 0; i < TA_NUM_LISTS; i++) {
      struct tr_list *list = &tracer->rc.lists[i];
      igText(list_names[i]);
      igText("%d original surfaces", list->num_orig_surfs);
      igText("%d draw surfaces", list->num_surfs);
      igSeparator();

      total_orig_surfs += list->num_orig_surfs;
      total_surfs += list->num_surfs;
    }

    igText("%d total original surfaces", total_orig_surfs);
    igText("%d total draw surfaces", total_surfs);
    igText("%.2f kb index buffer", (tracer->rc.num_indices * 2.0f) / 1024.0f);

    igEnd();
  }
}

void tracer_render_frame(struct tracer *tracer) {
  r_clear(tracer->r);

  /* build ui */
  tracer_render_side_menu(tracer);
  tracer_render_scrubber_menu(tracer);
  tracer_render_debug_menu(tracer);

  /* render context up to the surface of the currently selected param */
  struct tr_context *rc = &tracer->rc;
  int end_surf = -1;

  if (tracer->current_param >= 0) {
    struct tr_param *rp = &rc->params[tracer->current_param];
    end_surf = rp->last_surf;
  }

  tr_convert_context(tracer->r, tracer, &tracer_find_texture, &tracer->ctx,
                     &tracer->rc);

  for (int i = 0; i < rc->num_surfs; i++) {
    struct ta_surface *surf = &rc->surfs[i];
    surf->params.debug_depth = tracer->debug_depth;
  }

  tr_render_context_until(tracer->r, rc, end_surf);
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

int tracer_keydown(struct tracer *tracer, int key, int16_t value) {
  if (key == K_LEFT && value) {
    tracer_prev_context(tracer);
  } else if (key == K_RIGHT && value) {
    tracer_next_context(tracer);
  } else if (key == K_UP && value) {
    tracer_prev_param(tracer);
  } else if (key == K_DOWN && value) {
    tracer_next_param(tracer);
  }

  return 0;
}

void tracer_vid_destroyed(struct tracer *tracer) {
  rb_for_each_entry_safe(tex, &tracer->live_textures, struct tracer_texture,
                         live_it) {
    r_destroy_texture(tracer->r, tex->handle);
    tex->handle = 0;
  }

  tracer->r = NULL;
}

void tracer_vid_created(struct tracer *tracer, struct render_backend *r) {
  tracer->r = r;
}

void tracer_destroy(struct tracer *tracer) {
  if (tracer->trace) {
    trace_destroy(tracer->trace);
  }

  tracer_vid_destroyed(tracer);

  free(tracer);
}

struct tracer *tracer_create(struct host *host) {
  struct tracer *tracer = calloc(1, sizeof(struct tracer));

  tracer->host = host;

  /* add all textures to free list */
  for (int i = 0, n = ARRAY_SIZE(tracer->textures); i < n; i++) {
    struct tracer_texture *tex = &tracer->textures[i];
    list_add(&tracer->free_textures, &tex->free_it);
  }

  return tracer;
}
