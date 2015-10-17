#include <algorithm>
#include "core/core.h"
#include "hw/holly/tile_accelerator.h"
#include "renderer/gl_backend.h"
#include "trace/trace.h"
#include "trace/trace_viewer.h"

using namespace dreavm;
using namespace dreavm::hw::holly;
using namespace dreavm::renderer;
using namespace dreavm::sys;
using namespace dreavm::trace;

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
    // TODO compare tex_it->tsp and tex_it->tcw with incoming?
    texture.handle = register_cb(texture.palette, texture.texture);
  }

  return texture.handle;
}

TraceViewer::TraceViewer() : tile_renderer_(texcache_) {
  rb_ = new GLBackend(wnd_);
  current_ctx_ = new TileContext();
}

TraceViewer::~TraceViewer() {
  delete current_ctx_;
  delete rb_;
}

void TraceViewer::Run(const char *path) {
  if (!Init()) {
    LOG_WARNING("Failed to initialize trace viewer");
    return;
  }

  if (!Parse(path)) {
    return;
  }

  while (true) {
    PumpEvents();

    RenderFrame();
  }
}

bool TraceViewer::Init() {
  if (!wnd_.Init()) {
    return false;
  }

  if (!rb_->Init()) {
    return false;
  }

  return true;
}

bool TraceViewer::Parse(const char *path) {
  if (!reader_.Parse(path)) {
    LOG_WARNING("Failed to parse %s", path);
    return false;
  }

  num_frames_ = GetNumFrames();
  if (!num_frames_) {
    LOG_WARNING("No frames in %s", path);
    return false;
  }

  current_frame_ = 0;
  current_cmd_ = nullptr;
  NextContext();

  return true;
}

void TraceViewer::PumpEvents() {
  WindowEvent ev;

  wnd_.PumpEvents();

  while (wnd_.PollEvent(&ev)) {
    switch (ev.type) {
      case WE_KEY: {
        if (ev.key.code == K_LEFT && ev.key.value) {
          PrevContext();
        } else if (ev.key.code == K_RIGHT && ev.key.value) {
          NextContext();
        }
      } break;

      default:
        break;
    }
  }
}

void TraceViewer::RenderFrame() {
  rb_->BeginFrame();

  tile_renderer_.RenderContext(current_ctx_, rb_);

  // render stats
  char stats[512];
  snprintf(stats, sizeof(stats), "frame %d / %d", current_frame_, num_frames_);
  rb_->RenderText2D(0, 0, 12.0f, 0xffffffff, stats);

  rb_->EndFrame();
}

int TraceViewer::GetNumFrames() {
  int num_frames = 0;

  TraceCommand *cmd = reader_.cmd_head();

  while (cmd) {
    if (cmd->type == TRACE_RENDER_CONTEXT) {
      num_frames++;
    }

    cmd = cmd->next;
  }

  return num_frames;
}

void TraceViewer::CopyCommandToContext(const TraceCommand *cmd,
                                       TileContext *ctx) {
  CHECK_EQ(cmd->type, TRACE_RENDER_CONTEXT);

  ctx->autosort = cmd->render_context.autosort;
  ctx->stride = cmd->render_context.stride;
  ctx->pal_pxl_format = cmd->render_context.pal_pxl_format;
  ctx->bg_isp = cmd->render_context.bg_isp;
  ctx->bg_tsp = cmd->render_context.bg_tsp;
  ctx->bg_tcw = cmd->render_context.bg_tcw;
  ctx->bg_depth = cmd->render_context.bg_depth;
  ctx->video_width = cmd->render_context.video_width;
  ctx->video_height = cmd->render_context.video_height;
  memcpy(ctx->bg_vertices, cmd->render_context.bg_vertices,
         cmd->render_context.bg_vertices_size);
  memcpy(ctx->data, cmd->render_context.data, cmd->render_context.data_size);
  ctx->size = cmd->render_context.data_size;
}

void TraceViewer::PrevContext() {
  int prev_frame = std::max(1, current_frame_ - 1);
  if (prev_frame == current_frame_) {
    return;
  }

  current_cmd_ = current_cmd_->prev;

  // scrub through commands until the previous context is reached. for each
  // command we move backwards through, re-apply the value it overrode
  while (current_cmd_) {
    TraceCommand *override = current_cmd_->override;

    if (current_cmd_->type == TRACE_INSERT_TEXTURE) {
      texcache_.RemoveTexture(current_cmd_->insert_texture.tsp,
                              current_cmd_->insert_texture.tcw);

      if (override) {
        CHECK_EQ(override->type, TRACE_INSERT_TEXTURE);
        texcache_.AddTexture(
            override->insert_texture.tsp, override->insert_texture.tcw,
            override->insert_texture.palette, override->insert_texture.texture);
      }
    } else if (current_cmd_->type == TRACE_RENDER_CONTEXT) {
      if (--current_frame_ == prev_frame) {
        break;
      }
    }

    current_cmd_ = current_cmd_->prev;
  }

  CHECK_NOTNULL(current_cmd_);

  CopyCommandToContext(current_cmd_, current_ctx_);
}

void TraceViewer::NextContext() {
  int next_frame = std::min(num_frames_, current_frame_ + 1);
  if (next_frame == current_frame_) {
    return;
  }

  current_cmd_ = current_cmd_ ? current_cmd_->next : reader_.cmd_head();

  while (current_cmd_) {
    if (current_cmd_->type == TRACE_INSERT_TEXTURE) {
      texcache_.AddTexture(current_cmd_->insert_texture.tsp,
                           current_cmd_->insert_texture.tcw,
                           current_cmd_->insert_texture.palette,
                           current_cmd_->insert_texture.texture);
    } else if (current_cmd_->type == TRACE_RENDER_CONTEXT) {
      if (++current_frame_ == next_frame) {
        break;
      }
    }

    current_cmd_ = current_cmd_->next;
  }

  CHECK_NOTNULL(current_cmd_);

  // render the context
  CopyCommandToContext(current_cmd_, current_ctx_);
}
