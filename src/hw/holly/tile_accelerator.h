#ifndef TILE_ACCELERATOR_H
#define TILE_ACCELERATOR_H

#include <set>
#include <queue>
#include <unordered_map>
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
struct Register;

namespace holly {

class Holly;
class PVR2;

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

typedef std::unordered_map<uint32_t, TileContext *> TileContextMap;
typedef std::queue<TileContext *> TileContextQueue;

#define TA_DECLARE_R32_DELEGATE(name) uint32_t name##_read(Register &)
#define TA_DECLARE_W32_DELEGATE(name) void name##_write(Register &, uint32_t)

#define TA_REGISTER_R32_DELEGATE(name) \
  pvr_->reg(name##_OFFSET).read =      \
      make_delegate(&TileAccelerator::name##_read, this)
#define TA_REGISTER_W32_DELEGATE(name) \
  pvr_->reg(name##_OFFSET).write =     \
      make_delegate(&TileAccelerator::name##_write, this)

#define TA_R32_DELEGATE(name) \
  uint32_t TileAccelerator::name##_read(Register &reg)
#define TA_W32_DELEGATE(name) \
  void TileAccelerator::name##_write(Register &reg, uint32_t old_value)

class TileAccelerator : public Device,
                        public MemoryInterface,
                        public WindowInterface,
                        public TextureProvider {
  friend class PVR2;

 public:
  static int GetParamSize(const PCW &pcw, int vertex_type);
  static int GetPolyType(const PCW &pcw);
  static int GetVertexType(const PCW &pcw);

  TileAccelerator(Dreamcast &dc, renderer::Backend &rb);

  bool Init() final;

  renderer::TextureHandle GetTexture(
      const TileContext &tctx, const TSP &tsp, const TCW &tcw,
      RegisterTextureDelegate register_delegate) final;

 private:
  // MemoryInterface
  void MapPhysicalMemory(Memory &memory, MemoryMap &memmap) final;
  void WritePolyFIFO(uint32_t addr, uint32_t value);
  void WriteTextureFIFO(uint32_t addr, uint32_t value);

  // WindowInterface
  void OnPaint(bool show_main_menu) final;

  void ClearTextures();
  void ClearPendingTextures();
  void InvalidateTexture(TextureKey key);
  void InvalidateTexture(TextureCacheMap::iterator it);
  void HandleTextureWrite(const sys::Exception &ex, void *data);
  void HandlePaletteWrite(const sys::Exception &ex, void *data);

  void SoftReset();
  void InitContext(uint32_t addr);
  void WriteContext(uint32_t addr, uint32_t value);
  void FinalizeContext(uint32_t addr);
  void SaveRegisterState(TileContext *tctx);

  void ToggleTracing();

  TA_DECLARE_W32_DELEGATE(SOFTRESET);
  TA_DECLARE_W32_DELEGATE(TA_LIST_INIT);
  TA_DECLARE_W32_DELEGATE(TA_LIST_CONT);
  TA_DECLARE_W32_DELEGATE(STARTRENDER);

  Dreamcast &dc_;
  renderer::Backend &rb_;
  TileRenderer tile_renderer_;
  Memory *memory_;
  Holly *holly_;
  PVR2 *pvr_;
  uint8_t *video_ram_;
  TraceWriter *trace_writer_;

  TextureCacheMap textures_;
  TextureSet pending_invalidations_;
  uint64_t num_invalidated_;

  TileContext tctxs_[MAX_CONTEXTS];
  TileContextMap live_tctxs_;
  TileContextQueue free_tctxs_;
  TileContext *last_tctx_;
};
}
}
}

#endif
