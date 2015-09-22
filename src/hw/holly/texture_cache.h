#ifndef TILE_TEXTURE_CACHE_H
#define TILE_TEXTURE_CACHE_H

#include <unordered_map>
#include "core/interval_tree.h"
#include "hw/holly/tile_renderer.h"
#include "renderer/backend.h"

namespace dreavm {
namespace trace {
class TraceWriter;
}

namespace hw {
class Dreamcast;

namespace holly {

struct TextureEntry;
typedef std::unordered_map<TextureKey, TextureEntry> TextureCacheMap;
typedef IntervalTree<TextureKey> TextureWatchTree;

struct TextureEntry {
  TextureEntry(renderer::TextureHandle handle)
      : handle(handle), texture_watch(nullptr), palette_watch(nullptr) {}

  renderer::TextureHandle handle;
  TextureWatchTree::node_type *texture_watch;
  TextureWatchTree::node_type *palette_watch;
};

class TextureCache : public TextureProvider {
 public:
  TextureCache(hw::Dreamcast *dc);

  bool Init();
  void CheckPaletteWrite(uint32_t offset);
  void CheckTextureWrite(uint32_t offset);
  renderer::TextureHandle GetTexture(const TSP &tsp, const TCW &tcw,
                                     RegisterTextureCallback register_cb);

 private:
  void Clear();
  void CheckWrite(uint32_t addr);
  void Invalidate(TextureKey key);
  void Invalidate(TextureCacheMap::iterator it);

  hw::Dreamcast *dc_;
  trace::TraceWriter *trace_writer_;
  TextureWatchTree watches_;
  TextureCacheMap textures_;
  uint64_t num_checks_;
  uint64_t num_invalidated_;
};
}
}
}

#endif
