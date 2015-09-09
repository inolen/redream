#ifndef TRACE_VIEWER_H
#define TRACE_VIEWER_H

#include <unordered_map>
#include <vector>
#include "holly/tile_renderer.h"
#include "system/system.h"
#include "trace/trace.h"

namespace dreavm {
namespace trace {

struct TextureInst {
  holly::TSP tsp;
  holly::TCW tcw;
  const uint8_t *palette;
  const uint8_t *texture;
  renderer::TextureHandle handle;
};

class TraceTextureCache : public holly::TextureCache {
 public:
  void AddTexture(const holly::TSP &tsp, holly::TCW &tcw,
                  const uint8_t *palette, const uint8_t *texture);
  void RemoveTexture(const holly::TSP &tsp, holly::TCW &tcw);

  renderer::TextureHandle GetTexture(
      const holly::TSP &tsp, const holly::TCW &tcw,
      holly::RegisterTextureCallback register_cb);

 private:
  std::unordered_map<uint32_t, TextureInst> textures_;
};

class TraceViewer {
 public:
  TraceViewer();
  ~TraceViewer();

  void Run(const char *path);

 private:
  bool Init();
  bool Parse(const char *path);

  void PumpEvents();
  void RenderFrame();

  int GetNumFrames();
  void CopyCommandToContext(const TraceCommand *cmd, holly::TileContext *ctx);
  void PrevContext();
  void NextContext();

  system::System sys_;
  TraceTextureCache texcache_;
  holly::TileRenderer tile_renderer_;
  renderer::Backend *rb_;

  TraceReader reader_;
  TraceCommand *current_cmd_;
  int num_frames_;
  int current_frame_;
  holly::TileContext current_ctx_;
};
}
}

#endif
