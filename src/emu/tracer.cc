#include <algorithm>
#include "core/memory.h"
#include "emu/tracer.h"
#include "hw/holly/tile_accelerator.h"
#include "hw/holly/trace.h"
#include "ui/window.h"

using namespace re;
using namespace re::emu;
using namespace re::hw::holly;
using namespace re::renderer;
using namespace re::ui;

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

static const int INVALID_OFFSET = -1;

void TraceTextureCache::AddTexture(const TSP &tsp, TCW &tcw,
                                   const uint8_t *palette,
                                   const uint8_t *texture) {
  TextureKey texture_key = TextureProvider::GetTextureKey(tsp, tcw);
  textures_[texture_key] =
      TextureInst{tsp, tcw, palette, texture, (TextureHandle)0};
}

void TraceTextureCache::RemoveTexture(const TSP &tsp, TCW &tcw) {
  TextureKey texture_key = TextureProvider::GetTextureKey(tsp, tcw);
  textures_.erase(texture_key);
}

TextureHandle TraceTextureCache::GetTexture(
    const TSP &tsp, const TCW &tcw, RegisterTextureCallback register_cb) {
  TextureKey texture_key = TextureProvider::GetTextureKey(tsp, tcw);

  auto it = textures_.find(texture_key);
  CHECK_NE(it, textures_.end(), "Texture wasn't available in cache");

  TextureInst &texture = it->second;

  // register the texture if it hasn't already been
  if (!texture.handle) {
    texture.handle = register_cb(texture.palette, texture.texture);
  }

  return texture.handle;
}

TextureHandle TraceTextureCache::GetTexture(hw::holly::TextureKey texture_key) {
  auto it = textures_.find(texture_key);
  if (it == textures_.end()) {
    return 0;
  }
  TextureInst &texture = it->second;
  return texture.handle;
}

Tracer::Tracer(Window &window)
    : window_(window),
      rb_(window.render_backend()),
      tile_renderer_(rb_, texcache_),
      hide_params_() {
  window_.AddListener(this);
}

Tracer::~Tracer() { window_.RemoveListener(this); }

void Tracer::Run(const char *path) {
  if (!Parse(path)) {
    return;
  }

  running_ = true;

  while (running_) {
    window_.PumpEvents();
  }
}

void Tracer::OnPaint(bool show_main_menu) {
  tile_renderer_.ParseContext(tctx_, &rctx_, true);

  // render UI
  RenderScrubberMenu();
  RenderTextureMenu();
  RenderContextMenu();

  // clamp surfaces the last surface belonging to the current param
  int n = static_cast<int>(rctx_.surfs.size());
  int last_idx = n;

  if (current_offset_ != INVALID_OFFSET) {
    const auto &param_entry = rctx_.param_map[current_offset_];
    last_idx = param_entry.num_surfs;
  }

  // render the context
  rb_.BeginSurfaces(rctx_.projection, rctx_.verts.data(),
                    static_cast<int>(rctx_.verts.size()));

  for (int i = 0; i < n; i++) {
    int idx = rctx_.sorted_surfs[i];

    // if this surface comes after the current parameter, ignore it
    if (idx >= last_idx) {
      continue;
    }

    rb_.DrawSurface(rctx_.surfs[idx]);
  }

  rb_.EndSurfaces();
}

void Tracer::OnKeyDown(Keycode code, int16_t value) {
  if (code == K_F1) {
    if (value) {
      window_.EnableMainMenu(!window_.MainMenuEnabled());
    }
  } else if (code == K_LEFT && value) {
    PrevContext();
  } else if (code == K_RIGHT && value) {
    NextContext();
  } else if (code == K_UP && value) {
    PrevParam();
  } else if (code == K_DOWN && value) {
    NextParam();
  }
}

void Tracer::OnClose() { running_ = false; }

bool Tracer::Parse(const char *path) {
  if (!trace_.Parse(path)) {
    LOG_WARNING("Failed to parse %s", path);
    return false;
  }

  ResetContext();

  return true;
}

void Tracer::RenderScrubberMenu() {
  ImGuiIO &io = ImGui::GetIO();

  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
  ImGui::Begin("Scrubber", nullptr,
               ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                   ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar);

  ImGui::SetWindowSize(ImVec2(io.DisplaySize.x, 0.0f));
  ImGui::SetWindowPos(ImVec2(0.0f, 0.0f));

  ImGui::PushItemWidth(-1.0f);
  int frame = current_frame_;
  if (ImGui::SliderInt("", &frame, 0, num_frames_ - 1)) {
    int delta = frame - current_frame_;
    for (int i = 0; i < std::abs(delta); i++) {
      if (delta > 0) {
        NextContext();
      } else {
        PrevContext();
      }
    }
  }
  ImGui::PopItemWidth();

  ImGui::End();
  ImGui::PopStyleVar();
}

void Tracer::RenderTextureMenu() {
  ImGuiIO &io = ImGui::GetIO();

  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
  ImGui::Begin("Textures", nullptr, ImGuiWindowFlags_NoTitleBar |
                                        ImGuiWindowFlags_NoResize |
                                        ImGuiWindowFlags_NoMove |
                                        ImGuiWindowFlags_HorizontalScrollbar);

  ImGui::SetWindowSize(ImVec2(io.DisplaySize.x, 0.0f));
  ImGui::SetWindowPos(
      ImVec2(0.0f, io.DisplaySize.y - ImGui::GetWindowSize().y));

  auto begin = texcache_.textures_begin();
  auto end = texcache_.textures_end();

  for (auto it = begin; it != end; ++it) {
    const TextureInst &tex = it->second;
    ImTextureID handle_id =
        reinterpret_cast<ImTextureID>(static_cast<intptr_t>(tex.handle));

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::ImageButton(handle_id, ImVec2(32.0f, 32.0f));
    ImGui::PopStyleColor();

    char popup_name[128];
    snprintf(popup_name, sizeof(popup_name), "texture_%d", tex.handle);

    if (ImGui::BeginPopupContextItem(popup_name, 0)) {
      ImGui::Image(handle_id, ImVec2(128, 128));
      ImGui::EndPopup();
    }

    ImGui::SameLine();
  }

  ImGui::End();
  ImGui::PopStyleVar();
}

void Tracer::FormatTooltip(const PolyParam *param, const Surface &surf) {
  int poly_type = TileAccelerator::GetPolyType(param->type0.pcw);

  ImGui::BeginTooltip();

  ImGui::Text("pcw: 0x%x", param->type0.pcw.full);
  ImGui::Text("isp_tsp: 0x%x", param->type0.isp_tsp.full);
  ImGui::Text("tsp: 0x%x", param->type0.tsp.full);
  ImGui::Text("tcw: 0x%x", param->type0.tcw.full);

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

  ImGui::Separator();

  ImGui::Image(
      reinterpret_cast<ImTextureID>(static_cast<intptr_t>(surf.texture)),
      ImVec2(64.0f, 64.0f));
  ImGui::Text("depth_write: %d", surf.depth_write);
  ImGui::Text("depth_func: %s", s_depthfunc_names[surf.depth_func]);
  ImGui::Text("cull: %s", s_cullface_names[surf.cull]);
  ImGui::Text("src_blend: %s", s_blendfunc_names[surf.src_blend]);
  ImGui::Text("dst_blend: %s", s_blendfunc_names[surf.dst_blend]);
  ImGui::Text("shade: %s", s_shademode_names[surf.shade]);
  ImGui::Text("ignore_tex_alpha: %d", surf.ignore_tex_alpha);
  ImGui::Text("first_vert: %d", surf.first_vert);
  ImGui::Text("num_verts: %d", surf.num_verts);

  ImGui::EndTooltip();
}

void Tracer::FormatTooltip(const VertexParam *param, const Vertex &vert,
                           int vertex_type) {
  ImGui::BeginTooltip();

  ImGui::Text("type: %d", vertex_type);

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

  ImGui::Separator();

  ImGui::Text("xyz: {%.2f, %.2f, %.2f}", vert.xyz[0], vert.xyz[1],
              vert.xyz[2]);
  ImGui::Text("uv: {%.2f, %.2f}", vert.uv[0], vert.uv[1]);
  ImGui::Text("color: 0x%08x", vert.color);
  ImGui::Text("offset_color: 0x%08x", vert.offset_color);

  ImGui::EndTooltip();
}

void Tracer::RenderContextMenu() {
  ImGui::Begin("Context", nullptr, ImVec2(256.0f, 256.0f));

  // param filters
  ImGui::Checkbox("Hide TA_PARAM_END_OF_LIST",
                  &hide_params_[TA_PARAM_END_OF_LIST]);
  ImGui::Checkbox("Hide TA_PARAM_USER_TILE_CLIP",
                  &hide_params_[TA_PARAM_USER_TILE_CLIP]);
  ImGui::Checkbox("Hide TA_PARAM_OBJ_LIST_SET",
                  &hide_params_[TA_PARAM_OBJ_LIST_SET]);
  ImGui::Checkbox("Hide TA_PARAM_POLY_OR_VOL",
                  &hide_params_[TA_PARAM_POLY_OR_VOL]);
  ImGui::Checkbox("Hide TA_PARAM_SPRITE", &hide_params_[TA_PARAM_SPRITE]);
  ImGui::Checkbox("Hide TA_PARAM_VERTEX", &hide_params_[TA_PARAM_VERTEX]);
  ImGui::Separator();

  // param list
  int vertex_type = 0;
  char label[128];

  for (auto it : rctx_.param_map) {
    int offset = it.first;
    PCW pcw = re::load<PCW>(tctx_.data + offset);
    bool param_selected = offset == current_offset_;

    if (!hide_params_[pcw.para_type]) {
      switch (pcw.para_type) {
        case TA_PARAM_END_OF_LIST: {
          snprintf(label, sizeof(label), "0x%04x TA_PARAM_END_OF_LIST", offset);
          ImGui::Selectable(label, &param_selected);
        } break;

        case TA_PARAM_USER_TILE_CLIP: {
          snprintf(label, sizeof(label), "0x%04x TA_PARAM_USER_TILE_CLIP",
                   offset);
          ImGui::Selectable(label, &param_selected);
        } break;

        case TA_PARAM_OBJ_LIST_SET: {
          snprintf(label, sizeof(label), "0x%04x TA_PARAM_OBJ_LIST_SET",
                   offset);
          ImGui::Selectable(label, &param_selected);
        } break;

        case TA_PARAM_POLY_OR_VOL: {
          const PolyParam *param =
              reinterpret_cast<const PolyParam *>(tctx_.data);

          vertex_type = TileAccelerator::GetVertexType(param->type0.pcw);

          snprintf(label, sizeof(label), "0x%04x TA_PARAM_POLY_OR_VOL", offset);
          ImGui::Selectable(label, &param_selected);

          if (ImGui::IsItemHovered()) {
            Surface &surf = rctx_.surfs[rctx_.param_map[offset].num_surfs - 1];
            FormatTooltip(param, surf);
          }
        } break;

        case TA_PARAM_SPRITE: {
          const PolyParam *param =
              reinterpret_cast<const PolyParam *>(tctx_.data);

          vertex_type = TileAccelerator::GetVertexType(param->type0.pcw);

          snprintf(label, sizeof(label), "0x%04x TA_PARAM_SPRITE", offset);
          ImGui::Selectable(label, &param_selected);

          if (ImGui::IsItemHovered()) {
            Surface &surf = rctx_.surfs[rctx_.param_map[offset].num_surfs - 1];
            FormatTooltip(param, surf);
          }
        } break;

        case TA_PARAM_VERTEX: {
          const VertexParam *param =
              reinterpret_cast<const VertexParam *>(tctx_.data);

          snprintf(label, sizeof(label), "0x%04x TA_PARAM_VERTEX", offset);
          ImGui::Selectable(label, &param_selected);

          if (ImGui::IsItemHovered()) {
            Vertex &vert = rctx_.verts[rctx_.param_map[offset].num_verts - 1];
            FormatTooltip(param, vert, vertex_type);
          }
        } break;

        default:
          LOG_FATAL("Unsupported parameter type %d", pcw.para_type);
          break;
      }

      if (param_selected) {
        if (!ImGui::IsItemVisible()) {
          ImGui::SetScrollHere();
        }

        current_offset_ = offset;
      }
    }
  }

  ImGui::End();
}

// Copy RENDER_CONTEXT command to the current context being rendered.
void Tracer::CopyCommandToContext(const TraceCommand *cmd, TileContext *ctx) {
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

void Tracer::PrevContext() {
  // get the prev context command
  TraceCommand *prev = current_cmd_->prev;
  while (prev) {
    if (prev->type == TRACE_CMD_CONTEXT) {
      break;
    }
    prev = prev->prev;
  }

  if (!prev) {
    return;
  }

  // skip forward to the last event for this frame
  TraceCommand *next = current_cmd_->next;
  TraceCommand *end = next;
  while (next) {
    if (next->type == TRACE_CMD_CONTEXT) {
      break;
    }
    end = next;
    next = next->next;
  }

  // revert all of the texture adds
  while (end) {
    if (end->type == TRACE_CMD_CONTEXT) {
      break;
    }

    if (end->type == TRACE_CMD_TEXTURE) {
      texcache_.RemoveTexture(prev->texture.tsp, prev->texture.tcw);

      TraceCommand *override = prev->override;
      if (override) {
        CHECK_EQ(override->type, TRACE_CMD_TEXTURE);

        texcache_.AddTexture(override->texture.tsp, override->texture.tcw,
                             override->texture.palette,
                             override->texture.texture);
      }
    }

    end = end->prev;
  }

  current_cmd_ = prev;

  // copy off the render command for the current context
  CopyCommandToContext(current_cmd_, &tctx_);

  // reset ui state
  current_frame_--;

  ResetParam();
}

void Tracer::NextContext() {
  // get the next context command
  TraceCommand *next = current_cmd_;

  if (!next) {
    next = trace_.cmds();
  } else {
    next = next->next;

    while (next) {
      if (next->type == TRACE_CMD_CONTEXT) {
        break;
      }

      next = next->next;
    }
  }

  // no next command
  if (!next) {
    return;
  }

  //
  current_cmd_ = next;

  // add any textures for this command
  next = current_cmd_->next;

  while (next) {
    if (next->type == TRACE_CMD_CONTEXT) {
      break;
    }

    if (next->type == TRACE_CMD_TEXTURE) {
      texcache_.AddTexture(next->texture.tsp, next->texture.tcw,
                           next->texture.palette, next->texture.texture);
    }

    next = next->next;
  }

  // copy off the context command to the current tile context
  CopyCommandToContext(current_cmd_, &tctx_);

  // point to the start of the next frame
  current_frame_++;

  ResetParam();
}

void Tracer::ResetContext() {
  // calculate the total number of frames for the trace
  TraceCommand *cmd = trace_.cmds();

  num_frames_ = 0;

  while (cmd) {
    if (cmd->type == TRACE_CMD_CONTEXT) {
      num_frames_++;
    }

    cmd = cmd->next;
  }

  current_frame_ = -1;
  current_cmd_ = nullptr;
  NextContext();
}

void Tracer::PrevParam() {
  auto it = rctx_.param_map.find(current_offset_);

  if (it == rctx_.param_map.end()) {
    return;
  }

  while (true) {
    // stop at first param
    if (it == rctx_.param_map.begin()) {
      break;
    }

    --it;

    int offset = it->first;
    PCW pcw = re::load<PCW>(tctx_.data + offset);

    // found the next visible param
    if (!hide_params_[pcw.para_type]) {
      current_offset_ = it->first;
      break;
    }
  }
}

void Tracer::NextParam() {
  auto it = rctx_.param_map.find(current_offset_);

  if (it == rctx_.param_map.end()) {
    return;
  }

  while (true) {
    ++it;

    // stop at last param
    if (it == rctx_.param_map.end()) {
      break;
    }

    int offset = it->first;
    PCW pcw = re::load<PCW>(tctx_.data + offset);

    // found the next visible param
    if (!hide_params_[pcw.para_type]) {
      current_offset_ = it->first;
      break;
    }
  }
}

void Tracer::ResetParam() { current_offset_ = INVALID_OFFSET; }
