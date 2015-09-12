#include "core/core.h"
#include "hw/dreamcast.h"

using namespace dreavm::hw;
using namespace dreavm::hw::holly;
using namespace dreavm::renderer;

TileTextureCache::TileTextureCache(Dreamcast *dc)
    : dc_(dc), trace_writer_(nullptr) {}

TextureHandle TileTextureCache::GetTexture(
    const TSP &tsp, const TCW &tcw, RegisterTextureCallback register_cb) {
  TextureKey texture_key = TextureCache::GetTextureKey(tsp, tcw);

  // if there are any pending removals, do so at this time
  if (pending_invalidations_.size()) {
    ClearPending();
  }

  // if the trace writer has changed, clear the cache to force insert events
  if (dc_->trace_writer() != trace_writer_) {
    ClearAll();
    trace_writer_ = dc_->trace_writer();
  }

  // see if an an entry already exists
  auto it = textures_.find(texture_key);
  if (it != textures_.end()) {
    return it->second;
  }

  // TCW texture_addr field is in 64-bit units
  uint32_t texture_addr = tcw.texture_addr << 3;

  // get the texture data
  uint8_t *video_ram = dc_->video_ram();
  uint8_t *texture = &video_ram[texture_addr];
  int width = 8 << tsp.texture_u_size;
  int height = 8 << tsp.texture_v_size;
  int element_size_bits = tcw.pixel_format == TA_PIXEL_8BPP
                              ? 8
                              : tcw.pixel_format == TA_PIXEL_4BPP ? 4 : 16;
  int texture_size = (width * height * element_size_bits) >> 3;

  // get the palette data
  uint8_t *palette = nullptr;
  int palette_size = 0;

  if (tcw.pixel_format == TA_PIXEL_4BPP || tcw.pixel_format == TA_PIXEL_8BPP) {
    // palette ram is 4096 bytes, with each palette entry being 4 bytes each,
    // resulting in 1 << 10 indexes
    if (tcw.pixel_format == TA_PIXEL_4BPP) {
      // in 4bpp mode, the palette selector represents the upper 6 bits of the
      // palette index, with the remaining 4 bits being filled in by the texture
      palette = dc_->palette_ram() + (tcw.p.palette_selector << 4) * 4;
      palette_size = (1 << 4) * 4;
    } else if (tcw.pixel_format == TA_PIXEL_8BPP) {
      // in 4bpp mode, the palette selector represents the upper 2 bits of the
      // palette index, with the remaining 8 bits being filled in by the texture
      palette = dc_->palette_ram() + ((tcw.p.palette_selector & 0x30) << 4) * 4;
      palette_size = (1 << 8) * 4;
    }
  }

  // register and insert into the cache
  TextureHandle handle = register_cb(palette, texture);
  auto result = textures_.insert(std::make_pair(texture_key, handle));
  CHECK(result.second, "Texture already in the map?");

  // add write callback in order to invalidate on future writes. the callback
  // address will be page aligned, therefore it will be triggered falsely in
  // some cases. over invalidate in these cases
  TileTextureCacheMap::value_type *map_entry = &(*result.first);

  auto callback = [](void *ctx, void *data) {
    TileTextureCache *texcache = reinterpret_cast<TileTextureCache *>(ctx);
    TileTextureCacheMap::value_type *map_entry =
        reinterpret_cast<TileTextureCacheMap::value_type *>(data);
    TextureKey texture_key = map_entry->first;

    // add to pending invalidation list (can't remove inside of signal handler)
    texcache->pending_invalidations_.insert(texture_key);
  };

  dc_->sigsegv()->AddWriteWatch(texture, texture_size, callback, this,
                                map_entry);

  // TODO generate id for watch, so it can be cleared by both callbacks
  // if (palette) {
  //   dc_->mmio()->AddWriteWatch(palette, palette_size, callback, this,
  //                              map_entry);
  // }

  // add insert to trace
  if (trace_writer_) {
    trace_writer_->WriteInsertTexture(tsp, tcw, palette, palette_size, texture,
                                      texture_size);
  }

  return handle;
}

void TileTextureCache::ClearPending() {
  for (auto texture_key : pending_invalidations_) {
    auto it = textures_.find(texture_key);
    CHECK_NE(it, textures_.end());

    TextureHandle handle = it->second;
    textures_.erase(it);

    dc_->rb()->FreeTexture(handle);
  }

  pending_invalidations_.clear();
}

void TileTextureCache::ClearAll() {
  for (auto it : textures_) {
    TextureHandle handle = it.second;

    dc_->rb()->FreeTexture(handle);
  }

  textures_.clear();

  pending_invalidations_.clear();
}
