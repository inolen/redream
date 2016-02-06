#ifndef TRACE_H
#define TRACE_H

#include "hw/holly/tile_accelerator.h"

namespace re {
namespace trace {

enum TraceCommandType { TRACE_INSERT_TEXTURE, TRACE_RENDER_CONTEXT };

struct TraceCommand {
  TraceCommand() : prev(nullptr), next(nullptr), override(nullptr) {}

  TraceCommandType type;

  // set on read
  TraceCommand *prev;
  TraceCommand *next;
  TraceCommand *override;

  // the data pointers in these structs are written out relative to the cmd,
  // and patched to absolute pointers on read
  union {
    struct {
      hw::holly::TSP tsp;
      hw::holly::TCW tcw;
      uint32_t palette_size;
      const uint8_t *palette;
      uint32_t texture_size;
      const uint8_t *texture;
    } insert_texture;

    // slimmed down version of the TileContext structure, will need to be in
    // sync
    struct {
      int8_t autosort;
      uint32_t stride;
      uint32_t pal_pxl_format;
      uint32_t video_width;
      uint32_t video_height;
      hw::holly::ISP_TSP bg_isp;
      hw::holly::TSP bg_tsp;
      hw::holly::TCW bg_tcw;
      float bg_depth;
      uint32_t bg_vertices_size;
      const uint8_t *bg_vertices;
      uint32_t data_size;
      const uint8_t *data;
    } render_context;
  };
};

extern void GetNextTraceFilename(char *filename, size_t size);

class TraceReader {
 public:
  TraceReader();
  ~TraceReader();

  TraceCommand *cmd_head() { return reinterpret_cast<TraceCommand *>(trace_); }

  bool Parse(const char *filename);

 private:
  void Reset();
  bool PatchPointers();
  bool PatchOverrides();

  size_t trace_size_;
  uint8_t *trace_;
};

class TraceWriter {
 public:
  TraceWriter();
  ~TraceWriter();

  bool Open(const char *filename);
  void Close();

  void WriteInsertTexture(const hw::holly::TSP &tsp, const hw::holly::TCW &tcw,
                          const uint8_t *palette, int palette_size,
                          const uint8_t *texture, int texture_size);
  void WriteRenderContext(hw::holly::TileContext *tactx);

 private:
  FILE *file_;
};
}
}

#endif
