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
  TextureInst(const holly::TSP &tsp, holly::TCW &tcw, const uint8_t *texture,
              const uint8_t *palette)
      : tsp(tsp), tcw(tcw), texture(texture), palette(palette), handle(0) {}

  holly::TSP tsp;
  holly::TCW tcw;
  const uint8_t *texture;
  const uint8_t *palette;
  renderer::TextureHandle handle;
};

class TraceTextureCache : public holly::TextureCache {
 public:
  void AddTexture(const holly::TSP &tsp, holly::TCW &tcw,
                  const uint8_t *texture, const uint8_t *palette);
  void RemoveTexture(const holly::TSP &tsp, holly::TCW &tcw);

  renderer::TextureHandle GetTexture(
      const holly::TSP &tsp, const holly::TCW &tcw,
      holly::RegisterTextureCallback register_cb);

 private:
  std::unordered_map<uint32_t, TextureInst> textures_;
};

class TraceViewer {
 public:
  TraceViewer(system::System &sys);
  ~TraceViewer();

  bool Init();
  bool Load(const char *path);
  void Tick();

 private:
  void PumpEvents();
  void RenderFrame();

  int GetNumFrames();
  void CopyCommandToContext(const TraceCommand *cmd, holly::TileContext *ctx);
  void PrevContext();
  void NextContext();

  system::System &sys_;
  TraceTextureCache texcache_;
  holly::TileRenderer renderer_;
  renderer::Backend *rb_;

  TraceReader reader_;
  TraceCommand *current_cmd_;
  int num_frames_;
  int current_frame_;
};
}
}

#endif
