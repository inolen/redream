#ifndef TILE_TEXTURE_CACHE_H
#define TILE_TEXTURE_CACHE_H

#include <set>
#include "hw/holly/tile_renderer.h"
#include "renderer/backend.h"

namespace dreavm {
namespace trace {
class TraceWriter;
}

namespace hw {
class Dreamcast;

namespace holly {

typedef std::unordered_map<TextureKey, renderer::TextureHandle>
    TileTextureCacheMap;

class TileTextureCache : public TextureCache {
 public:
  TileTextureCache(hw::Dreamcast *dc);

  renderer::TextureHandle GetTexture(const TSP &tsp, const TCW &tcw,
                                     RegisterTextureCallback register_cb);

 private:
  void ClearPending();
  void ClearAll();

  hw::Dreamcast *dc_;
  trace::TraceWriter *trace_writer_;
  TileTextureCacheMap textures_;
  std::set<TextureKey> pending_invalidations_;
};
}
}
}

#endif
