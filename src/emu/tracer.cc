#include <imgui.h>
#include "core/math.h"
#include "emu/tracer.h"
#include "hw/holly/ta.h"
#include "hw/holly/tr.h"
#include "hw/holly/trace.h"
#include "ui/window.h"

static const char *s_param_names[] = {
    "TA_PARAM_END_OF_LIST", "TA_PARAM_USER_TILE_CLIP", "TA_PARAM_OBJ_LIST_SET",
    "TA_PARAM_RESERVED0",   "TA_PARAM_POLY_OR_VOL",    "TA_PARAM_SPRITE",
    "TA_PARAM_RESERVED1",   "TA_PARAM_VERTEX",
};

static const char *s_list_names[] = {
    "TA_LIST_OPAQUE",        "TA_LIST_OPAQUE_MODVOL",
    "TA_LIST_TRANSLUCENT",   "TA_LIST_TRANSLUCENT_MODVOL",
    "TA_LIST_PUNCH_THROUGH",
};

static const char *s_pixel_format_names[] = {
    "PXL_INVALID", "PXL_RGBA",     "PXL_RGBA5551",
    "PXL_RGB565",  "PXL_RGBA4444", "PXL_RGBA8888",
};

static const char *s_filter_mode_names[] = {
    "FILTER_NEAREST", "FILTER_BILINEAR",
};

static const char *s_wrap_mode_names[] = {
    "WRAP_REPEAT", "WRAP_CLAMP_TO_EDGE", "WRAP_MIRRORED_REPEAT",
};

static const char *s_depthfunc_names[] = {
    "NONE",    "NEVER",  "LESS",   "EQUAL",  "LEQUAL",
    "GREATER", "NEQUAL", "GEQUAL", "ALWAYS",
};

static const char *s_cullface_names[] = {
    "NONE", "FRONT", "BACK",
};

static const char *s_blendfunc_names[] = {
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

static const char *s_shademode_names[] = {
    "DECAL", "MODULATE", "DECAL_ALPHA", "MODULATE_ALPHA",
};

typedef struct {
  union tsp tsp;
  union tcw tcw;
  const uint8_t *palette;
  const uint8_t *data;
  texture_handle_t handle;
  enum pxl_format format;
  enum filter_mode filter;
  enum wrap_mode wrap_u;
  enum wrap_mode wrap_v;
  bool mipmaps;
  int width;
  int height;

  struct rb_node live_it;
  struct list_node free_it;
} texture_entry_t;

typedef struct tracer_s {
  struct window *window;
  struct window_listener *listener;
  struct rb *rb;
  struct tr *tr;

  // trace state
  trace_t *trace;
  struct tile_ctx ctx;
  trace_cmd_t *current_cmd;
  int current_param_offset;
  int current_context;
  int num_contexts;

  // ui state
  bool hide_params[TA_NUM_PARAMS];
  bool scroll_to_param;
  bool running;

  // render state
  struct render_ctx rctx;
  struct surface surfs[TA_MAX_SURFS];
  struct vertex verts[TA_MAX_VERTS];
  int sorted_surfs[TA_MAX_SURFS];
  struct param_state states[TA_PARAMS_SIZE];

  texture_entry_t textures[1024];
  struct rb_tree live_textures;
  struct list free_textures;
} tracer_t;

static int tracer_texture_cmp(const struct rb_node *rb_lhs,
                              const struct rb_node *rb_rhs) {
  const texture_entry_t *lhs = rb_entry(rb_lhs, const texture_entry_t, live_it);
  const texture_entry_t *rhs = rb_entry(rb_rhs, const texture_entry_t, live_it);
  return tr_get_texture_key(lhs->tsp, lhs->tcw) -
         tr_get_texture_key(rhs->tsp, rhs->tcw);
}

static struct rb_callbacks tracer_texture_cb = {&tracer_texture_cmp, NULL,
                                                NULL};

static void tracer_add_texture(tracer_t *tracer, union tsp tsp, union tcw tcw,
                               const uint8_t *palette, const uint8_t *texture) {
  texture_entry_t *entry =
      list_first_entry(&tracer->free_textures, texture_entry_t, free_it);
  CHECK_NOTNULL(entry);
  list_remove(&tracer->free_textures, &entry->free_it);

  entry->tsp = tsp;
  entry->tcw = tcw;
  entry->palette = palette;
  entry->texture = texture;
  entry->handle = 0;

  rb_insert(&tracer->live_textures, &entry->live_it, &tracer_texture_cb);
}

static void tracer_remove_texture(tracer_t *tracer, union tsp tsp,
                                  union tcw tcw) {
  texture_entry_t search;
  search.tsp = tsp;
  search.tcw = tcw;

  texture_entry_t *entry = rb_find_entry(&tracer->live_textures, &search,
                                         live_it, &tracer_texture_cb);
  CHECK_NOTNULL(entry);
  rb_unlink(&tracer->live_textures, &texture->live_it, &tracer_texture_cb);

  list_add(&tracer->free_textures, &texture->free_it);
}

static texture_handle_t tracer_get_texture(void *data,
                                           const struct tile_ctx *ctx,
                                           union tsp tsp, union tcw tcw,
                                           void *register_data,
                                           register_texture_cb register_cb) {
  tracer_t *tracer = reinterpret_cast<tracer_t *>(data);

  texture_entry_t search;
  search.tsp = tsp;
  search.tcw = tcw;

  texture_entry_t *entry = rb_find_entry(&tracer->live_textures, &search,
                                         live_it, &tracer_texture_cb);
  CHECK_NOTNULL(texture, "Texture wasn't available in cache");

  // TODO fixme, pass correct struct tile_ctx to tracer_add_texture so this
  // isn't deferred
  if (!entry->handle) {
    struct texture_reg reg = {};
    reg.ctx = ctx;
    reg.tsp = tsp;
    reg.tcw = tcw;
    reg.palette = entry->palette;
    reg.texture = entry->texture;
    register_cb(register_data, &reg);

    entry->handle = reg.handle;
    entry->format = reg.format;
    entry->filter = reg.filter;
    entry->wrap_u = reg.wrap_u;
    entry->wrap_v = reg.wrap_v;
    entry->mipmaps = reg.mipmaps;
    entry->width = reg.width;
    entry->height = reg.height;
  }

  return entry->handle;
}

static void tracer_copy_command(const trace_cmd_t *cmd, struct tile_ctx *ctx) {
  // copy TRACE_CMD_CONTEXT to the current context being rendered
  CHECK_EQ(cmd->type, TRACE_CMD_CONTEXT);

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
  memcpy(ctx->data, cmd->context.data, cmd->context.data_size);
  ctx->size = cmd->context.data_size;
}

static inline bool param_state_empty(struct param_state *param_state) {
  return !param_state->num_surfs && !param_state->num_verts;
}

static inline bool tracer_param_hidden(tracer_t *tracer, union pcw pcw) {
  return tracer->hide_params[pcw.para_type];
}

static void tracer_prev_param(tracer_t *tracer) {
  int offset = tracer->current_param_offset;

  while (--offset >= 0) {
    struct param_state *param_state = &tracer->rctx.states[offset];

    if (param_state_empty(param_state)) {
      continue;
    }

    union pcw pcw = *(union pcw *)(tracer->ctx.data + offset);

    // found the next visible param
    if (!tracer_param_hidden(tracer, pcw)) {
      tracer->current_param_offset = offset;
      tracer->scroll_to_param = true;
      break;
    }
  }
}

static void tracer_next_param(tracer_t *tracer) {
  int offset = tracer->current_param_offset;

  while (++offset < tracer->rctx.num_states) {
    struct param_state *param_state = &tracer->rctx.states[offset];

    if (param_state_empty(param_state)) {
      continue;
    }

    union pcw pcw = *(union pcw *)(tracer->ctx.data + offset);

    // found the next visible param
    if (!tracer_param_hidden(tracer, pcw)) {
      tracer->current_param_offset = offset;
      tracer->scroll_to_param = true;
      break;
    }
  }
}

static void tracer_reset_param(tracer_t *tracer) {
  tracer->current_param_offset = -1;
  tracer->scroll_to_param = false;
}

static void tracer_prev_context(tracer_t *tracer) {
  trace_cmd_t *begin = tracer->current_cmd->prev;

  // ensure that there is a prev context
  trace_cmd_t *prev = begin;

  while (prev) {
    if (prev->type == TRACE_CMD_CONTEXT) {
      break;
    }

    prev = prev->prev;
  }

  if (!prev) {
    return;
  }

  // walk back to the prev context, reverting any textures that've been added
  trace_cmd_t *curr = begin;

  while (curr != prev) {
    if (curr->type == TRACE_CMD_TEXTURE) {
      tracer_remove_texture(tracer, curr->texture.tsp, curr->texture.tcw);

      trace_cmd_t * override = curr->override;
      if (override) {
        CHECK_EQ(override->type, TRACE_CMD_TEXTURE);

        tracer_add_texture(tracer, override->texture.tsp, override->texture.tcw,
                           override->texture.palette,
                           override->texture.texture);
      }
    }

    curr = curr->prev;
  }

  tracer->current_cmd = curr;
  tracer->current_context--;
  tracer_copy_command(tracer->current_cmd, &tracer->ctx);
  tracer_reset_param(tracer);
}

static void tracer_next_context(tracer_t *tracer) {
  trace_cmd_t *begin =
      tracer->current_cmd ? tracer->current_cmd->next : tracer->trace->cmds;

  // ensure that there is a next context
  trace_cmd_t *next = begin;

  while (next) {
    if (next->type == TRACE_CMD_CONTEXT) {
      break;
    }

    next = next->next;
  }

  if (!next) {
    return;
  }

  // walk towards to the next context, adding any new textures
  trace_cmd_t *curr = begin;

  while (curr != next) {
    if (curr->type == TRACE_CMD_TEXTURE) {
      tracer_add_texture(tracer, curr->texture.tsp, curr->texture.tcw,
                         curr->texture.palette, curr->texture.texture);
    }

    curr = curr->next;
  }

  tracer->current_cmd = curr;
  tracer->current_context++;
  tracer_copy_command(tracer->current_cmd, &tracer->ctx);
  tracer_reset_param(tracer);
}

static void tracer_reset_context(tracer_t *tracer) {
  // calculate the total number of frames for the trace
  trace_cmd_t *cmd = tracer->trace->cmds;

  tracer->num_contexts = 0;

  while (cmd) {
    if (cmd->type == TRACE_CMD_CONTEXT) {
      tracer->num_contexts++;
    }

    cmd = cmd->next;
  }

  // start rendering the first context
  tracer->current_cmd = NULL;
  tracer->current_context = -1;
  tracer_next_context(tracer);
}

static void tracer_render_scrubber_menu(tracer_t *tracer) {
  ImGuiIO &io = ImGui::GetIO();

  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
  ImGui::Begin("Scrubber", NULL,
               ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                   ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar);

  ImGui::SetWindowSize(ImVec2(io.DisplaySize.x, 0.0f));
  ImGui::SetWindowPos(ImVec2(0.0f, 0.0f));

  ImGui::PushItemWidth(-1.0f);
  int frame = tracer->current_context;
  if (ImGui::SliderInt("", &frame, 0, tracer->num_contexts - 1)) {
    int delta = frame - tracer->current_context;
    for (int i = 0; i < ABS(delta); i++) {
      if (delta > 0) {
        tracer_next_context(tracer);
      } else {
        tracer_prev_context(tracer);
      }
    }
  }
  ImGui::PopItemWidth();

  ImGui::End();
  ImGui::PopStyleVar();
}

static void tracer_render_texture_menu(tracer_t *tracer) {
  ImGuiIO &io = ImGui::GetIO();

  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
  ImGui::Begin("Textures", NULL, ImGuiWindowFlags_NoTitleBar |
                                     ImGuiWindowFlags_NoResize |
                                     ImGuiWindowFlags_NoMove |
                                     ImGuiWindowFlags_HorizontalScrollbar);

  ImGui::SetWindowSize(ImVec2(io.DisplaySize.x, 0.0f));
  ImGui::SetWindowPos(
      ImVec2(0.0f, io.DisplaySize.y - ImGui::GetWindowSize().y));

  rb_for_each_entry(tex, &tracer->live_textures, texture_entry_t, live_it) {
    ImTextureID handle_id =
        reinterpret_cast<ImTextureID>(static_cast<intptr_t>(tex->handle));

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::ImageButton(handle_id, ImVec2(32.0f, 32.0f), ImVec2(0.0f, 1.0f),
                       ImVec2(1.0f, 0.0f));
    ImGui::PopStyleColor();

    char popup_name[128];
    snprintf(popup_name, sizeof(popup_name), "texture_%d", tex->handle);

    if (ImGui::BeginPopupContextItem(popup_name, 0)) {
      ImGui::Image(handle_id, ImVec2(128, 128), ImVec2(0.0f, 1.0f),
                   ImVec2(1.0f, 0.0f));

      ImGui::Separator();

      ImGui::Text("addr; 0x%08x", tex->tcw.texture_addr << 3);
      ImGui::Text("format: %s", s_pixel_format_names[tex->format]);
      ImGui::Text("filter: %s", s_filter_mode_names[tex->filter]);
      ImGui::Text("wrap_u: %s", s_wrap_mode_names[tex->wrap_u]);
      ImGui::Text("wrap_v: %s", s_wrap_mode_names[tex->wrap_v]);
      ImGui::Text("mipmaps: %d", tex->mipmaps);
      ImGui::Text("width: %d", tex->width);
      ImGui::Text("height: %d", tex->height);

      ImGui::EndPopup();
    }

    ImGui::SameLine();
  }

  ImGui::End();
  ImGui::PopStyleVar();
}

static void tracer_format_tooltip(tracer_t *tracer, int list_type,
                                  int vertex_type, int offset) {
  struct param_state *param_state = &tracer->rctx.states[offset];
  int surf_id = param_state->num_surfs - 1;
  int vert_id = param_state->num_verts - 1;

  ImGui::BeginTooltip();

  ImGui::Text("list type: %s", s_list_names[list_type]);
  ImGui::Text("surf: %d", surf_id);

  {
    // find sorted position
    int sort = 0;
    for (int i = 0, n = tracer->rctx.num_surfs; i < n; i++) {
      int idx = tracer->rctx.sorted_surfs[i];
      if (idx == surf_id) {
        sort = i;
        break;
      }
    }
    ImGui::Text("sort: %d", sort);
  }

  // render source TA information
  if (vertex_type == -1) {
    const union poly_param *param =
        reinterpret_cast<const union poly_param *>(tracer->ctx.data + offset);

    ImGui::Text("pcw: 0x%x", param->type0.pcw.full);
    ImGui::Text("isp_tsp: 0x%x", param->type0.isp_tsp.full);
    ImGui::Text("tsp: 0x%x", param->type0.tsp.full);
    ImGui::Text("tcw: 0x%x", param->type0.tcw.full);

    int poly_type = ta_get_poly_type(param->type0.pcw);

    switch (poly_type) {
      case 1:
        ImGui::Text("face_color_a: %.2f", param->type1.face_color_a);
        ImGui::Text("face_color_r: %.2f", param->type1.face_color_r);
        ImGui::Text("face_color_g: %.2f", param->type1.face_color_g);
        ImGui::Text("face_color_b: %.2f", param->type1.face_color_b);
        break;

      case 2:
        ImGui::Text("face_color_a: %.2f", param->type2.face_color_a);
        ImGui::Text("face_color_r: %.2f", param->type2.face_color_r);
        ImGui::Text("face_color_g: %.2f", param->type2.face_color_g);
        ImGui::Text("face_color_b: %.2f", param->type2.face_color_b);
        ImGui::Text("face_offset_color_a: %.2f",
                    param->type2.face_offset_color_a);
        ImGui::Text("face_offset_color_r: %.2f",
                    param->type2.face_offset_color_r);
        ImGui::Text("face_offset_color_g: %.2f",
                    param->type2.face_offset_color_g);
        ImGui::Text("face_offset_color_b: %.2f",
                    param->type2.face_offset_color_b);
        break;

      case 5:
        ImGui::Text("base_color: 0x%x", param->sprite.base_color);
        ImGui::Text("offset_color: 0x%x", param->sprite.offset_color);
        break;
    }
  } else {
    const union vert_param *param =
        reinterpret_cast<const union vert_param *>(tracer->ctx.data + offset);

    ImGui::Text("vert type: %d", vertex_type);

    switch (vertex_type) {
      case 0:
        ImGui::Text("xyz: {%.2f, %.2f, %.2f}", param->type0.xyz[0],
                    param->type0.xyz[1], param->type0.xyz[2]);
        ImGui::Text("base_color: 0x%x", param->type0.base_color);
        break;

      case 1:
        ImGui::Text("xyz: {%.2f, %.2f, %.2f}", param->type1.xyz[0],
                    param->type1.xyz[1], param->type1.xyz[2]);
        ImGui::Text("base_color_a: %.2f", param->type1.base_color_a);
        ImGui::Text("base_color_r: %.2f", param->type1.base_color_r);
        ImGui::Text("base_color_g: %.2f", param->type1.base_color_g);
        ImGui::Text("base_color_b: %.2f", param->type1.base_color_b);
        break;

      case 2:
        ImGui::Text("xyz: {%.2f, %.2f, %.2f}", param->type2.xyz[0],
                    param->type2.xyz[1], param->type2.xyz[2]);
        ImGui::Text("base_intensity: %.2f", param->type2.base_intensity);
        break;

      case 3:
        ImGui::Text("xyz: {%.2f, %.2f, %.2f}", param->type3.xyz[0],
                    param->type3.xyz[1], param->type3.xyz[2]);
        ImGui::Text("uv: {%.2f, %.2f}", param->type3.uv[0], param->type3.uv[1]);
        ImGui::Text("base_color: 0x%x", param->type3.base_color);
        ImGui::Text("offset_color: 0x%x", param->type3.offset_color);
        break;

      case 4:
        ImGui::Text("xyz: {%.2f, %.2f, %.2f}", param->type4.xyz[0],
                    param->type4.xyz[1], param->type4.xyz[2]);
        ImGui::Text("uv: {0x%x, 0x%x}", param->type4.uv[0], param->type4.uv[1]);
        ImGui::Text("base_color: 0x%x", param->type4.base_color);
        ImGui::Text("offset_color: 0x%x", param->type4.offset_color);
        break;

      case 5:
        ImGui::Text("xyz: {%.2f, %.2f, %.2f}", param->type5.xyz[0],
                    param->type5.xyz[1], param->type5.xyz[2]);
        ImGui::Text("uv: {%.2f, %.2f}", param->type5.uv[0], param->type5.uv[1]);
        ImGui::Text("base_color_a: %.2f", param->type5.base_color_a);
        ImGui::Text("base_color_r: %.2f", param->type5.base_color_r);
        ImGui::Text("base_color_g: %.2f", param->type5.base_color_g);
        ImGui::Text("base_color_b: %.2f", param->type5.base_color_b);
        ImGui::Text("offset_color_a: %.2f", param->type5.offset_color_a);
        ImGui::Text("offset_color_r: %.2f", param->type5.offset_color_r);
        ImGui::Text("offset_color_g: %.2f", param->type5.offset_color_g);
        ImGui::Text("offset_color_b: %.2f", param->type5.offset_color_b);
        break;

      case 6:
        ImGui::Text("xyz: {%.2f, %.2f, %.2f}", param->type6.xyz[0],
                    param->type6.xyz[1], param->type6.xyz[2]);
        ImGui::Text("uv: {0x%x, 0x%x}", param->type6.uv[0], param->type6.uv[1]);
        ImGui::Text("base_color_a: %.2f", param->type6.base_color_a);
        ImGui::Text("base_color_r: %.2f", param->type6.base_color_r);
        ImGui::Text("base_color_g: %.2f", param->type6.base_color_g);
        ImGui::Text("base_color_b: %.2f", param->type6.base_color_b);
        ImGui::Text("offset_color_a: %.2f", param->type6.offset_color_a);
        ImGui::Text("offset_color_r: %.2f", param->type6.offset_color_r);
        ImGui::Text("offset_color_g: %.2f", param->type6.offset_color_g);
        ImGui::Text("offset_color_b: %.2f", param->type6.offset_color_b);
        break;

      case 7:
        ImGui::Text("xyz: {%.2f, %.2f, %.2f}", param->type7.xyz[0],
                    param->type7.xyz[1], param->type7.xyz[2]);
        ImGui::Text("uv: {%.2f, %.2f}", param->type7.uv[0], param->type7.uv[1]);
        ImGui::Text("base_intensity: %.2f", param->type7.base_intensity);
        ImGui::Text("offset_intensity: %.2f", param->type7.offset_intensity);
        break;

      case 8:
        ImGui::Text("xyz: {%.2f, %.2f, %.2f}", param->type8.xyz[0],
                    param->type8.xyz[1], param->type8.xyz[2]);
        ImGui::Text("uv: {0x%x, 0x%x}", param->type8.uv[0], param->type8.uv[1]);
        ImGui::Text("base_intensity: %.2f", param->type8.base_intensity);
        ImGui::Text("offset_intensity: %.2f", param->type8.offset_intensity);
        break;
    }
  }

  // always render translated surface information. new surfaces can be created
  // without receiving a new TA_PARAM_POLY_OR_VOL / TA_PARAM_SPRITE
  struct surface *surf = &tracer->rctx.surfs[surf_id];

  ImGui::Separator();

  ImGui::Image(
      reinterpret_cast<ImTextureID>(static_cast<intptr_t>(surf->texture)),
      ImVec2(64.0f, 64.0f));
  ImGui::Text("depth_write: %d", surf->depth_write);
  ImGui::Text("depth_func: %s", s_depthfunc_names[surf->depth_func]);
  ImGui::Text("cull: %s", s_cullface_names[surf->cull]);
  ImGui::Text("src_blend: %s", s_blendfunc_names[surf->src_blend]);
  ImGui::Text("dst_blend: %s", s_blendfunc_names[surf->dst_blend]);
  ImGui::Text("shade: %s", s_shademode_names[surf->shade]);
  ImGui::Text("ignore_tex_alpha: %d", surf->ignore_tex_alpha);
  ImGui::Text("first_vert: %d", surf->first_vert);
  ImGui::Text("num_verts: %d", surf->num_verts);

  // render translated vert only when rendering a vertex tooltip
  if (vertex_type != -1) {
    struct vertex *vert = &tracer->rctx.verts[vert_id];

    ImGui::Separator();

    ImGui::Text("xyz: {%.2f, %.2f, %.2f}", vert->xyz[0], vert->xyz[1],
                vert->xyz[2]);
    ImGui::Text("uv: {%.2f, %.2f}", vert->uv[0], vert->uv[1]);
    ImGui::Text("color: 0x%08x", vert->color);
    ImGui::Text("offset_color: 0x%08x", vert->offset_color);
  }

  ImGui::EndTooltip();
}

static void tracer_render_context_menu(tracer_t *tracer) {
  char label[128];

  ImGui::Begin("Context", NULL, ImVec2(256.0f, 256.0f));

  // param filters
  for (int i = 0; i < TA_NUM_PARAMS; i++) {
    snprintf(label, sizeof(label), "Hide %s", s_param_names[i]);
    ImGui::Checkbox(label, &tracer->hide_params[i]);
  }
  ImGui::Separator();

  // param list
  int list_type = 0;
  int vertex_type = 0;

  for (int offset = 0; offset < tracer->rctx.num_states; offset++) {
    struct param_state *param_state = &tracer->rctx.states[offset];

    if (param_state_empty(param_state)) {
      continue;
    }

    union pcw pcw = *(union pcw *)(tracer->ctx.data + offset);
    bool param_selected = (offset == tracer->current_param_offset);

    if (!tracer_param_hidden(tracer, pcw)) {
      snprintf(label, sizeof(label), "0x%04x %s", offset,
               s_param_names[pcw.para_type]);
      ImGui::Selectable(label, &param_selected);

      switch (pcw.para_type) {
        case TA_PARAM_POLY_OR_VOL:
        case TA_PARAM_SPRITE: {
          const union poly_param *param =
              reinterpret_cast<const union poly_param *>(tracer->ctx.data +
                                                         offset);
          list_type = param->type0.pcw.list_type;
          vertex_type = ta_get_vert_type(param->type0.pcw);

          if (ImGui::IsItemHovered()) {
            tracer_format_tooltip(tracer, list_type, -1, offset);
          }
        } break;

        case TA_PARAM_VERTEX: {
          if (ImGui::IsItemHovered()) {
            tracer_format_tooltip(tracer, list_type, vertex_type, offset);
          }
        } break;
      }

      if (param_selected) {
        tracer->current_param_offset = offset;

        if (tracer->scroll_to_param) {
          if (!ImGui::IsItemVisible()) {
            ImGui::SetScrollHere();
          }

          tracer->scroll_to_param = false;
        }
      }
    }
  }

  ImGui::End();
}

static void tracer_onpaint(tracer_t *tracer, bool show_main_menu) {
  tr_parse_context(tracer->tr, &tracer->ctx, &tracer->rctx);

  // render UI
  tracer_render_scrubber_menu(tracer);
  tracer_render_texture_menu(tracer);
  tracer_render_context_menu(tracer);

  // clamp surfaces the last surface belonging to the current param
  int n = tracer->rctx.num_surfs;
  int last_idx = n;

  if (tracer->current_param_offset >= 0) {
    const struct param_state *offset =
        &tracer->rctx.states[tracer->current_param_offset];
    last_idx = offset->num_surfs;
  }

  // render the context
  rb_begin_surfaces(tracer->rb, tracer->rctx.projection, tracer->rctx.verts,
                    tracer->rctx.num_verts);

  for (int i = 0; i < n; i++) {
    int idx = tracer->rctx.sorted_surfs[i];

    // if this surface comes after the current parameter, ignore it
    if (idx >= last_idx) {
      continue;
    }

    rb_draw_surface(tracer->rb, &tracer->rctx.surfs[idx]);
  }

  rb_end_surfaces(tracer->rb);
}

static void tracer_onkeydown(tracer_t *tracer, enum keycode code,
                             int16_t value) {
  if (code == K_F1) {
    if (value) {
      win_enable_main_menu(tracer->window, !tracer->window->main_menu);
    }
  } else if (code == K_LEFT && value) {
    tracer_prev_context(tracer);
  } else if (code == K_RIGHT && value) {
    tracer_next_context(tracer);
  } else if (code == K_UP && value) {
    tracer_prev_param(tracer);
  } else if (code == K_DOWN && value) {
    tracer_next_param(tracer);
  }
}

static void tracer_onclose(tracer_t *tracer) {
  tracer->running = false;
}

static bool tracer_parse(tracer_t *tracer, const char *path) {
  if (tracer->trace) {
    trace_destroy(tracer->trace);
    tracer->trace = NULL;
  }

  tracer->trace = trace_parse(path);

  if (!tracer->trace) {
    LOG_WARNING("Failed to parse %s", path);
    return false;
  }

  tracer_reset_context(tracer);

  return true;
}

void tracer_run(tracer_t *tracer, const char *path) {
  if (!tracer_parse(tracer, path)) {
    return;
  }

  tracer->running = true;

  while (tracer->running) {
    win_pump_events(tracer->window);
  }
}

tracer_t *tracer_create(struct window *window) {
  static const struct window_callbacks callbacks = {
      NULL,
      (window_paint_cb)&tracer_onpaint,
      NULL,
      (window_keydown_cb)&tracer_onkeydown,
      NULL,
      NULL,
      (window_close_cb)&tracer_onclose};

  // ensure param / poly / vertex size LUTs are generated
  ta_build_tables();

  tracer_t *tracer = reinterpret_cast<tracer_t *>(calloc(1, sizeof(tracer_t)));

  tracer->window = window;
  tracer->listener = win_add_listener(window, &callbacks, tracer);
  tracer->rb = window->rb;
  tracer->tr = tr_create(tracer->rb, tracer, &tracer_get_texture);

  // setup render context buffers
  tracer->rctx.surfs = tracer->surfs;
  tracer->rctx.surfs_size = array_size(tracer->surfs);
  tracer->rctx.verts = tracer->verts;
  tracer->rctx.verts_size = array_size(tracer->verts);
  tracer->rctx.sorted_surfs = tracer->sorted_surfs;
  tracer->rctx.sorted_surfs_size = array_size(tracer->sorted_surfs);
  tracer->rctx.states = tracer->states;
  tracer->rctx.states_size = array_size(tracer->states);

  // add all textures to free list
  for (int i = 0, n = array_size(tracer->textures); i < n; i++) {
    texture_entry_t *entry = &tracer->textures[i];
    list_add(&tracer->free_textures, &entry->free_it);
  }

  return tracer;
}

void tracer_destroy(tracer_t *tracer) {
  if (tracer->trace) {
    trace_destroy(tracer->trace);
  }
  win_remove_listener(tracer->window, tracer->listener);
  tr_destroy(tracer->tr);
  free(tracer);
}
