#ifndef TILE_TEXTURE_CACHE_H
#define TILE_TEXTURE_CACHE_H

#include <set>
#include <unordered_map>
#include "core/interval_tree.h"
#include "hw/holly/tile_renderer.h"
#include "hw/memory.h"
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
typedef std::set<TextureKey> TextureSet;

struct TextureEntry {
  TextureEntry(renderer::TextureHandle handle)
      : handle(handle), texture_watch(nullptr), palette_watch(nullptr) {}

  renderer::TextureHandle handle;
  sys::WatchHandle texture_watch;
  sys::WatchHandle palette_watch;
};

class TextureCache : public TextureProvider {
 public:
  TextureCache(hw::Dreamcast *dc);

  bool Init();
  renderer::TextureHandle GetTexture(const TSP &tsp, const TCW &tcw,
                                     RegisterTextureCallback register_cb);

 private:
  static void HandleTextureWrite(void *ctx, const sys::Exception &ex,
                                 void *data);
  static void HandlePaletteWrite(void *ctx, const sys::Exception &ex,
                                 void *data);

  void Clear();
  void ClearPending();
  void Invalidate(TextureKey key);
  void Invalidate(TextureCacheMap::iterator it);

  hw::Dreamcast *dc_;
  trace::TraceWriter *trace_writer_;
  TextureCacheMap textures_;
  TextureSet pending_invalidations_;
  uint64_t num_invalidated_;
};
}
}
}

#endif
