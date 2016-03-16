#include <algorithm>
#include "emu/tracer.h"
#include "hw/holly/tile_accelerator.h"
#include "hw/holly/trace.h"
#include "ui/window.h"

using namespace re;
using namespace re::emu;
using namespace re::hw::holly;
using namespace re::renderer;
using namespace re::ui;

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

Tracer::Tracer(Window &window)
    : window_(window), tile_renderer_(*window.render_backend(), texcache_) {
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
  tile_renderer_.RenderContext(&current_ctx_);

  if (ImGui::Begin("Tracer")) {
    int frame = current_cmd_->frame;

    if (ImGui::SliderInt("frame", &frame, 0, num_frames_ - 1)) {
      SetFrame(frame);
    }

    ImGui::Separator();

    ImGui::LabelText("autosort", "%d", current_ctx_.autosort);
    ImGui::LabelText("texture stride", "%d", current_ctx_.stride);
    ImGui::LabelText("palette pixel format", "0x%08x",
                     current_ctx_.pal_pxl_format);
    ImGui::LabelText("bg isp", "0x%08x", current_ctx_.bg_isp.full);
    ImGui::LabelText("bg tsp", "0x%08x", current_ctx_.bg_tsp.full);
    ImGui::LabelText("bg tcw", "0x%08x", current_ctx_.bg_tcw.full);
    ImGui::LabelText("bg depth", "%.2f", current_ctx_.bg_depth);

    ImGui::End();
  }
}

void Tracer::OnKeyDown(Keycode code, int16_t value) {
  if (code == K_LEFT && value) {
    SetFrame(current_cmd_->frame - 1);
  } else if (code == K_RIGHT && value) {
    SetFrame(current_cmd_->frame + 1);
  }
}

void Tracer::OnClose() { running_ = false; }

bool Tracer::Parse(const char *path) {
  if (!reader_.Parse(path)) {
    LOG_WARNING("Failed to parse %s", path);
    return false;
  }

  num_frames_ = GetNumFrames();
  if (!num_frames_) {
    LOG_WARNING("No frames in %s", path);
    return false;
  }

  current_cmd_ = nullptr;
  SetFrame(0);

  return true;
}

int Tracer::GetNumFrames() {
  int num_frames = 0;

  TraceCommand *cmd = reader_.cmd_head();

  while (cmd) {
    if (cmd->type == TRACE_CMD_CONTEXT) {
      num_frames++;
    }

    cmd = cmd->next;
  }

  return num_frames;
}

// Se the current frame to be rendered. Note, due to textures being inserted on
// demand after the render event, the event order will look like so:
// RENDER_CONTEXT frame 0
// INSERT_TEXTURE frame 0
// INSERT_TEXTURE frame 0
// INSERT_TEXTURE frame 0
// RENDER_CONTEXT frame 1
// INSERT_TEXTURE frame 1
// INSERT_TEXTURE frame 1
// RENDER_CONTEXT frame 2
// INSERT_TEXTURE frame 2
void Tracer::SetFrame(int n) {
  n = std::max(0, std::min(n, num_frames_ - 1));

  // current_cmd_ is either null, or the first command of the current frame
  if (!current_cmd_ || n > current_cmd_->frame) {
    TraceCommand *next = current_cmd_ ? current_cmd_->next : reader_.cmd_head();

    // step forward until all events up to the end of the target frame have
    // been processed
    while (next && next->frame <= n) {
      if (next->type == TRACE_CMD_CONTEXT) {
        // track the beginning of the target frame
        current_cmd_ = next;
      } else if (next->type == TRACE_CMD_TEXTURE) {
        texcache_.AddTexture(next->texture.tsp, next->texture.tcw,
                             next->texture.palette, next->texture.texture);
      }

      next = next->next;
    }
  } else if (n < current_cmd_->frame) {
    TraceCommand *prev = current_cmd_;

    // step backwards until the start of the requested frame
    while (prev && prev->frame >= n) {
      if (prev->type == TRACE_CMD_CONTEXT) {
        // track the beginning of the target frame
        current_cmd_ = prev;
      } else if (prev->type == TRACE_CMD_TEXTURE) {
        // if the texture belongs to a frame after the target frame, remove it
        // from the cache and add back the texture it overrode (if it did)
        if (prev->frame > n) {
          texcache_.RemoveTexture(prev->texture.tsp, prev->texture.tcw);

          TraceCommand *override = prev->override;
          if (override) {
            CHECK_EQ(override->type, TRACE_CMD_TEXTURE);

            texcache_.AddTexture(override->texture.tsp, override->texture.tcw,
                                 override->texture.palette,
                                 override->texture.texture);
          }
        }
      }

      prev = prev->prev;
    }
  }

  // copy off the render command for the current frame
  CopyCommandToContext(current_cmd_, &current_ctx_);
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
