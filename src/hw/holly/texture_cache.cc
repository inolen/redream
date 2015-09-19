#include "core/core.h"
#include "emu/profiler.h"
#include "hw/dreamcast.h"

using namespace dreavm::hw;
using namespace dreavm::hw::holly;
using namespace dreavm::renderer;

TextureCache::TextureCache(Dreamcast *dc)
    : dc_(dc), trace_writer_(nullptr), num_checks_(0), num_invalidated_(0) {}

bool TextureCache::Init() { return true; }

void TextureCache::CheckPaletteWrite(uint32_t offset) {
  CheckWrite(PVR_PALETTE_START + offset);
}

void TextureCache::CheckTextureWrite(uint32_t offset) {
  CheckWrite(PVR_VRAM32_START + offset);
}

TextureHandle TextureCache::GetTexture(const TSP &tsp, const TCW &tcw,
                                       RegisterTextureCallback register_cb) {
  TextureKey texture_key = TextureProvider::GetTextureKey(tsp, tcw);

  // if the trace writer has changed, clear the cache to force insert events
  if (dc_->trace_writer() != trace_writer_) {
    Clear();
    trace_writer_ = dc_->trace_writer();
  }

  // see if an an entry already exists
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

  // add write watches in order to invalidate on future writes
  TextureEntry &entry = result.first->second;

  entry.texture_watch = watches_.Insert(
      PVR_VRAM32_START + texture_addr,
      PVR_VRAM32_START + texture_addr + texture_size - 1, texture_key);

  if (palette) {
    entry.palette_watch = watches_.Insert(
        PVR_PALETTE_START + palette_addr,
        PVR_PALETTE_START + palette_addr + palette_size - 1, texture_key);
  }

  PROFILER_COUNT("TextureCache watches", watches_.Size());

  // add insert to trace
  if (trace_writer_) {
    trace_writer_->WriteInsertTexture(tsp, tcw, palette, palette_size, texture,
                                      texture_size);
  }

  return handle;
}

void TextureCache::Clear() {
  for (auto it = textures_.begin(), e = textures_.end(); it != e; ++it) {
    Invalidate(it);
  }
}

void TextureCache::CheckWrite(uint32_t addr) {
  PROFILER_GPU("TextureCache::CheckWrite");

  TextureWatchTree::Node *node = watches_.Find(addr, addr);

  bool handled = node != nullptr;

  while (node) {
    Invalidate(node->value);

    node = watches_.Find(addr, addr);
  }

  // monitor cache invalidation success
  num_checks_++;

  if (handled) {
    num_invalidated_++;
  }

  PROFILER_COUNT("TextureCache num checks", num_checks_);
  PROFILER_COUNT("TextureCache num invalidated", num_invalidated_);
}

void TextureCache::Invalidate(TextureKey texture_key) {
  auto it = textures_.find(texture_key);
  CHECK_NE(it, textures_.end());
  Invalidate(it);
}

void TextureCache::Invalidate(TextureCacheMap::iterator it) {
  TextureEntry &entry = it->second;

  watches_.Remove(entry.texture_watch);

  if (entry.palette_watch) {
    watches_.Remove(entry.palette_watch);
  }

  dc_->rb()->FreeTexture(entry.handle);

  textures_.erase(it);
}
