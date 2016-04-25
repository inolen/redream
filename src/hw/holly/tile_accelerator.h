#ifndef TILE_ACCELERATOR_H
#define TILE_ACCELERATOR_H

#include <set>
#include <queue>
#include <unordered_map>
#include "hw/holly/tile_accelerator_types.h"
#include "hw/holly/tile_renderer.h"
#include "hw/holly/trace.h"
#include "hw/machine.h"
#include "hw/memory.h"
#include "hw/register.h"
#include "renderer/backend.h"
#include "sys/memory.h"

namespace re {
namespace hw {

class Dreamcast;

namespace sh4 {
class SH4;
}

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

class TileAccelerator : public Device,
                        public WindowInterface,
                        public TextureProvider {
  friend class PVR2;

 public:
  static int GetParamSize(const PCW &pcw, int vertex_type);
  static int GetPolyType(const PCW &pcw);
  static int GetVertexType(const PCW &pcw);

  AM_DECLARE(fifo_map);

  TileAccelerator(Dreamcast &dc, renderer::Backend *rb);

  bool Init() final;

  renderer::TextureHandle GetTexture(
      const TileContext &tctx, const TSP &tsp, const TCW &tcw,
      RegisterTextureDelegate register_delegate) final;

 private:
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

  DECLARE_W32_DELEGATE(SOFTRESET);
  DECLARE_W32_DELEGATE(TA_LIST_INIT);
  DECLARE_W32_DELEGATE(TA_LIST_CONT);
  DECLARE_W32_DELEGATE(STARTRENDER);

  Dreamcast &dc_;
  renderer::Backend *rb_;
  TileRenderer tile_renderer_;
  sh4::SH4 *sh4_;
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
