#include <unordered_map>
#include "core/core.h"
#include "trace/trace.h"

using namespace dreavm::core;
using namespace dreavm::holly;
using namespace dreavm::trace;

void dreavm::trace::GetNextTraceFilename(char *filename, size_t size) {
  const char *appdir = GetAppDir();

  for (int i = 0; i < INT_MAX; i++) {
    snprintf(filename, size, "%s" PATH_SEPARATOR "%d.trace", appdir, i);

    if (!Exists(filename)) {
      return;
    }
  }

  LOG(FATAL) << "Unable to find available trace filename";
}

TraceReader::TraceReader() : trace_size_(0), trace_(nullptr) {}

TraceReader::~TraceReader() {
  Reset();
}

bool TraceReader::Parse(const char *filename) {
  Reset();

  FILE *fp = fopen(filename, "rb");
  if (!fp) {
    return false;
  }

  fseek(fp, 0, SEEK_END);
  trace_size_ = ftell(fp);
  fseek(fp, 0, SEEK_SET);

  trace_ = new uint8_t[trace_size_];
  CHECK_EQ(fread(trace_, trace_size_, 1, fp), 1);
  fclose(fp);

  if (!PatchPointers()) {
    return false;
  }

  if (!PatchOverrides()) {
    return false;
  }

  return true;
}

void TraceReader::Reset() {
  if (trace_) {
    delete[] trace_;
  }
}

// Commands are written out with null list pointers, and pointers to data
// are written out relative to the command itself. Set the list pointers,
// and make the data pointers absolute.
bool TraceReader::PatchPointers() {
  TraceCommand *prev_cmd = nullptr;
  TraceCommand *curr_cmd = nullptr;
  uint8_t *ptr = trace_;
  uint8_t *end = trace_ + trace_size_;

  while (ptr < end) {
    prev_cmd = curr_cmd;
    curr_cmd = reinterpret_cast<TraceCommand *>(ptr);

    // set prev / next pointers
    if (prev_cmd) prev_cmd->next = curr_cmd;
    curr_cmd->prev = prev_cmd;
    curr_cmd->next = nullptr;
    curr_cmd->override = nullptr;

    // patch relative data pointers
    switch (curr_cmd->type) {
      case TRACE_RESIZE_VIDEO: {
        ptr += sizeof(*curr_cmd);
      } break;

      case TRACE_INSERT_TEXTURE: {
        curr_cmd->insert_texture.texture += reinterpret_cast<intptr_t>(ptr);
        curr_cmd->insert_texture.palette += reinterpret_cast<intptr_t>(ptr);
        ptr += sizeof(*curr_cmd) + curr_cmd->insert_texture.texture_size +
               curr_cmd->insert_texture.palette_size;
      } break;

      case TRACE_RENDER_CONTEXT: {
        curr_cmd->render_context.bg_vertices += reinterpret_cast<intptr_t>(ptr);
        curr_cmd->render_context.data += reinterpret_cast<intptr_t>(ptr);
        ptr += sizeof(*curr_cmd) + curr_cmd->render_context.bg_vertices_size +
               curr_cmd->render_context.data_size;
      } break;

      default:
        LOG(INFO) << "Unexpected trace command type " << curr_cmd->type;
        return false;
    }
  }

  return true;
}

// For commands which mutate global state, the previous state needs to be
// tracked in order to support unwinding. To do so, each command is iterated
// and tagged with the previous command that it overrides.
bool TraceReader::PatchOverrides() {
  TraceCommand *cmd = cmd_head();

  TraceCommand *last_resize = nullptr;
  std::unordered_map<uint32_t, TraceCommand *> last_inserts;

  while (cmd) {
    switch (cmd->type) {
      case TRACE_RESIZE_VIDEO: {
        if (last_resize) {
          cmd->override = last_resize;
        }
        last_resize = cmd;
      } break;

      case TRACE_INSERT_TEXTURE: {
        uint32_t texture_key = TextureCache::GetTextureKey(
            cmd->insert_texture.tsp, cmd->insert_texture.tcw);
        auto last_insert = last_inserts.find(texture_key);

        if (last_insert != last_inserts.end()) {
          cmd->override = last_insert->second;
          last_insert->second = cmd;
        } else {
          last_inserts.insert(std::make_pair(texture_key, cmd));
        }
      } break;

      case TRACE_RENDER_CONTEXT: {
      } break;

      default:
        LOG(INFO) << "Unexpected trace command type " << cmd->type;
        return false;
    }

    cmd = cmd->next;
  }

  return true;
}

TraceWriter::TraceWriter() : file_(nullptr) {}

TraceWriter::~TraceWriter() {
  Close();
}

bool TraceWriter::Open(const char *filename) {
  Close();

  file_ = fopen(filename, "wb");

  return !!file_;
}

void TraceWriter::Close() {
  if (file_) {
    fclose(file_);
  }
}

void TraceWriter::WriteResizeVideo(int width, int height) {
  TraceCommand cmd;
  cmd.type = TRACE_RESIZE_VIDEO;
  cmd.resize_video.width = width;
  cmd.resize_video.height = height;

  CHECK_EQ(fwrite(&cmd, sizeof(cmd), 1, file_), 1);
}

void TraceWriter::WriteInsertTexture(const TSP &tsp, const TCW &tcw,
                                     const uint8_t *texture, int texture_size,
                                     const uint8_t *palette, int palette_size) {
  TraceCommand cmd;
  cmd.type = TRACE_INSERT_TEXTURE;
  cmd.insert_texture.tsp = tsp;
  cmd.insert_texture.tcw = tcw;
  cmd.insert_texture.texture_size = texture_size;
  cmd.insert_texture.texture = reinterpret_cast<const uint8_t *>(sizeof(cmd));
  cmd.insert_texture.palette_size = palette_size;
  cmd.insert_texture.palette =
      reinterpret_cast<const uint8_t *>(sizeof(cmd) + texture_size);

  CHECK_EQ(fwrite(&cmd, sizeof(cmd), 1, file_), 1);
  if (texture_size) {
    CHECK_EQ(fwrite(texture, texture_size, 1, file_), 1);
  }
  if (palette_size) {
    CHECK_EQ(fwrite(palette, palette_size, 1, file_), 1);
  }
}

void TraceWriter::WriteRenderContext(TileContext *tactx) {
  TraceCommand cmd;
  cmd.type = TRACE_RENDER_CONTEXT;
  cmd.render_context.autosort = tactx->autosort;
  cmd.render_context.stride = tactx->stride;
  cmd.render_context.pal_pxl_format = tactx->pal_pxl_format;
  cmd.render_context.bg_isp = tactx->bg_isp;
  cmd.render_context.bg_tsp = tactx->bg_tsp;
  cmd.render_context.bg_tcw = tactx->bg_tcw;
  cmd.render_context.bg_vertices_size = sizeof(tactx->bg_vertices);
  cmd.render_context.bg_vertices =
      reinterpret_cast<const uint8_t *>(sizeof(cmd));
  cmd.render_context.data_size = tactx->size;
  cmd.render_context.data = reinterpret_cast<const uint8_t *>(
      sizeof(cmd) + sizeof(tactx->bg_vertices));

  CHECK_EQ(fwrite(&cmd, sizeof(cmd), 1, file_), 1);
  CHECK_EQ(fwrite(tactx->bg_vertices, sizeof(tactx->bg_vertices), 1, file_), 1);
  if (tactx->size) {
    CHECK_EQ(fwrite(tactx->data, tactx->size, 1, file_), 1);
  }
}
