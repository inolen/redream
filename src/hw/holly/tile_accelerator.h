#ifndef TILE_ACCELERATOR_H
#define TILE_ACCELERATOR_H

#include <memory>
#include <set>
#include <queue>
#include <unordered_map>
#include "core/interval_tree.h"
#include "hw/holly/tile_accelerator_types.h"
#include "hw/holly/tile_renderer.h"
#include "hw/holly/trace.h"
#include "hw/machine.h"
#include "renderer/backend.h"
#include "sys/memory.h"

namespace re {
namespace hw {

class Dreamcast;
class Memory;

namespace holly {

class Holly;

static const int MAX_CONTEXTS = 8;

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

typedef std::unordered_map<TextureKey, TileContext *> TileContextMap;
typedef std::queue<TileContext *> TileContextQueue;

class TileAccelerator : public Device,
                        public MemoryInterface,
                        public WindowInterface,
                        public TextureProvider {
  friend class PVR2;

 public:
  static int GetParamSize(const PCW &pcw, int vertex_type);
  static int GetPolyType(const PCW &pcw);
  static int GetVertexType(const PCW &pcw);

  TileAccelerator(Dreamcast *dc, renderer::Backend *rb);

  bool Init() final;

  renderer::TextureHandle GetTexture(const TSP &tsp, const TCW &tcw,
                                     RegisterTextureCallback register_cb) final;

  void SoftReset();
  void InitContext(uint32_t addr);
  void WriteContext(uint32_t addr, uint32_t value);
  void FinalizeContext(uint32_t addr);
  TileContext *GetLastContext();

 protected:
  // MemoryInterface
  void MapPhysicalMemory(Memory &memory, MemoryMap &memmap) final;

  // WindowInterface
  void OnPaint(bool show_main_menu) final;

 private:
  void ToggleTracing();

  void ClearTextures();
  void ClearPendingTextures();
  void InvalidateTexture(TextureKey key);
  void InvalidateTexture(TextureCacheMap::iterator it);
  void HandleTextureWrite(const sys::Exception &ex, void *data);
  void HandlePaletteWrite(const sys::Exception &ex, void *data);

  void WritePolyFIFO(uint32_t addr, uint32_t value);
  void WriteTextureFIFO(uint32_t addr, uint32_t value);

  void SaveRegisterState(TileContext *tactx);

  Dreamcast *dc_;
  renderer::Backend *rb_;
  Memory *memory_;
  holly::Holly *holly_;
  uint8_t *video_ram_;
  hw::holly::TileRenderer tile_renderer_;
  TraceWriter *trace_writer_;

  TextureCacheMap textures_;
  TextureSet pending_invalidations_;
  uint64_t num_invalidated_;

  TileContext contexts_[MAX_CONTEXTS];
  TileContextMap live_contexts_;
  TileContextQueue free_contexts_;
  TileContextQueue pending_contexts_;
};
}
}
}

#endif
