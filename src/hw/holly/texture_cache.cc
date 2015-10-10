#include "core/core.h"
#include "emu/profiler.h"
#include "hw/dreamcast.h"

using namespace dreavm::hw;
using namespace dreavm::hw::holly;
using namespace dreavm::renderer;
using namespace dreavm::sys;

TextureCache::TextureCache(Dreamcast *dc)
    : dc_(dc), trace_writer_(nullptr), num_invalidated_(0) {}

bool TextureCache::Init() { return true; }

TextureHandle TextureCache::GetTexture(const TSP &tsp, const TCW &tcw,
                                       RegisterTextureCallback register_cb) {
  // if there are any pending removals, do so at this time
  if (pending_invalidations_.size()) {
    ClearPending();
  }

  // if the trace writer has changed, clear the cache to force insert events
  if (dc_->trace_writer() != trace_writer_) {
    Clear();
    trace_writer_ = dc_->trace_writer();
  }

  // see if an an entry already exists
  TextureKey texture_key = TextureProvider::GetTextureKey(tsp, tcw);

  auto it = textures_.find(texture_key);
  if (it != textures_.end()) {
    return it->second.handle;
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
  uint8_t *palette_ram = dc_->palette_ram();
  uint8_t *palette = nullptr;
  uint32_t palette_addr = 0;
  int palette_size = 0;

  if (tcw.pixel_format == TA_PIXEL_4BPP || tcw.pixel_format == TA_PIXEL_8BPP) {
    // palette ram is 4096 bytes, with each palette entry being 4 bytes each,
    // resulting in 1 << 10 indexes
    if (tcw.pixel_format == TA_PIXEL_4BPP) {
      // in 4bpp mode, the palette selector represents the upper 6 bits of the
      // palette index, with the remaining 4 bits being filled in by the texture
      palette_addr = (tcw.p.palette_selector << 4) * 4;
      palette_size = (1 << 4) * 4;
    } else if (tcw.pixel_format == TA_PIXEL_8BPP) {
      // in 4bpp mode, the palette selector represents the upper 2 bits of the
      // palette index, with the remaining 8 bits being filled in by the texture
      palette_addr = ((tcw.p.palette_selector & 0x30) << 4) * 4;
      palette_size = (1 << 8) * 4;
    }

    palette = &palette_ram[palette_addr];
  }

  // register and insert into the cache
  TextureHandle handle = register_cb(palette, texture);
  auto result =
      textures_.insert(std::make_pair(texture_key, TextureEntry(handle)));
  CHECK(result.second, "Texture already in the map?");

  // add write callback in order to invalidate on future writes. the callback
  // address will be page aligned, therefore it will be triggered falsely in
  // some cases. over invalidate in these cases
  TextureEntry &entry = result.first->second;
  TextureCacheMap::value_type *map_entry = &(*result.first);

  entry.texture_watch = SIGSEGVHandler::instance()->AddSingleWriteWatch(
      texture, texture_size, &HandleTextureWrite, this, map_entry);

  if (palette) {
    entry.palette_watch = SIGSEGVHandler::instance()->AddSingleWriteWatch(
        palette, palette_size, &HandlePaletteWrite, this, map_entry);
  }

  // add insert to trace
  if (trace_writer_) {
    trace_writer_->WriteInsertTexture(tsp, tcw, palette, palette_size, texture,
                                      texture_size);
  }

  return handle;
}

void TextureCache::HandleTextureWrite(void *ctx, void *data, uintptr_t rip,
                                      uintptr_t fault_addr) {
  TextureCache *texcache = reinterpret_cast<TextureCache *>(ctx);
  TextureCacheMap::value_type *map_entry =
      reinterpret_cast<TextureCacheMap::value_type *>(data);

  // don't double remove the watch during invalidation
  TextureEntry &entry = map_entry->second;
  entry.texture_watch = nullptr;

  // add to pending invalidation list (can't remove inside of signal
  // handler)
  TextureKey texture_key = map_entry->first;
  texcache->pending_invalidations_.insert(texture_key);
}

void TextureCache::HandlePaletteWrite(void *ctx, void *data, uintptr_t rip,
                                      uintptr_t fault_addr) {
  TextureCache *texcache = reinterpret_cast<TextureCache *>(ctx);
  TextureCacheMap::value_type *map_entry =
      reinterpret_cast<TextureCacheMap::value_type *>(data);

  // don't double remove the watch during invalidation
  TextureEntry &entry = map_entry->second;
  entry.palette_watch = nullptr;

  // add to pending invalidation list (can't remove inside of signal
  // handler)
  TextureKey texture_key = map_entry->first;
  texcache->pending_invalidations_.insert(texture_key);
}

void TextureCache::Clear() {
  for (auto it = textures_.begin(), e = textures_.end(); it != e; ++it) {
    Invalidate(it);
  }
}

void TextureCache::ClearPending() {
  for (auto texture_key : pending_invalidations_) {
    auto it = textures_.find(texture_key);
    CHECK_NE(it, textures_.end());
    Invalidate(it);
  }

  num_invalidated_ += pending_invalidations_.size();
  PROFILER_COUNT("Num invalidated textures", num_invalidated_);

  pending_invalidations_.clear();
}

void TextureCache::Invalidate(TextureKey texture_key) {
  auto it = textures_.find(texture_key);

  // multiple writes may have already invalidated this texture
  if (it == textures_.end()) {
    return;
  }

  Invalidate(it);
}

void TextureCache::Invalidate(TextureCacheMap::iterator it) {
  TextureEntry &entry = it->second;

  if (entry.texture_watch) {
    SIGSEGVHandler::instance()->RemoveWatch(entry.texture_watch);
  }

  if (entry.palette_watch) {
    SIGSEGVHandler::instance()->RemoveWatch(entry.palette_watch);
  }

  dc_->rb()->FreeTexture(entry.handle);

  textures_.erase(it);
}
