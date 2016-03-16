#include "core/memory.h"
#include "emu/profiler.h"
#include "hw/holly/holly.h"
#include "hw/holly/pixel_convert.h"
#include "hw/holly/tile_accelerator.h"
#include "hw/holly/trace.h"
#include "hw/dreamcast.h"
#include "hw/memory.h"

using namespace re;
using namespace re::hw;
using namespace re::hw::holly;
using namespace re::renderer;
using namespace re::sys;

static void BuildLookupTables();
static int GetParamSize_raw(const PCW &pcw, int vertex_type);
static int GetPolyType_raw(const PCW &pcw);
static int GetVertexType_raw(const PCW &pcw);

static HollyInterrupt list_interrupts[] = {
    HOLLY_INTC_TAEOINT,   // TA_LIST_OPAQUE
    HOLLY_INTC_TAEOMINT,  // TA_LIST_OPAQUE_MODVOL
    HOLLY_INTC_TAETINT,   // TA_LIST_TRANSLUCENT
    HOLLY_INTC_TAETMINT,  // TA_LIST_TRANSLUCENT_MODVOL
    HOLLY_INTC_TAEPTIN    // TA_LIST_PUNCH_THROUGH
};

static int param_size_lookup[0x100 * TA_NUM_PARAMS * TA_NUM_VERT_TYPES];
static int poly_type_lookup[0x100 * TA_NUM_PARAMS * TA_NUM_LISTS];
static int vertex_type_lookup[0x100 * TA_NUM_PARAMS * TA_NUM_LISTS];

static struct _ta_lookup_tables_init {
  _ta_lookup_tables_init() { BuildLookupTables(); }
} ta_lookup_tables_init;

static void BuildLookupTables() {
  for (int i = 0; i < 0x100; i++) {
    PCW pcw = *(PCW *)&i;

    for (int j = 0; j < TA_NUM_PARAMS; j++) {
      pcw.para_type = j;

      for (int k = 0; k < TA_NUM_VERT_TYPES; k++) {
        param_size_lookup[i * TA_NUM_PARAMS * TA_NUM_VERT_TYPES +
                          j * TA_NUM_VERT_TYPES + k] = GetParamSize_raw(pcw, k);
      }
    }
  }

  for (int i = 0; i < 0x100; i++) {
    PCW pcw = *(PCW *)&i;

    for (int j = 0; j < TA_NUM_PARAMS; j++) {
      pcw.para_type = j;

      for (int k = 0; k < TA_NUM_LISTS; k++) {
        pcw.list_type = k;

        poly_type_lookup[i * TA_NUM_PARAMS * TA_NUM_LISTS + j * TA_NUM_LISTS +
                         k] = GetPolyType_raw(pcw);
        vertex_type_lookup[i * TA_NUM_PARAMS * TA_NUM_LISTS + j * TA_NUM_LISTS +
                           k] = GetVertexType_raw(pcw);
      }
    }
  }
}

// Parameter size can be determined by only the PCW for every parameter other
// than vertex parameters. For vertex parameters, the vertex type derived from
// the last poly or modifier volume parameter is needed.
static int GetParamSize_raw(const PCW &pcw, int vertex_type) {
  switch (pcw.para_type) {
    case TA_PARAM_END_OF_LIST:
      return 32;
    case TA_PARAM_USER_TILE_CLIP:
      return 32;
    case TA_PARAM_OBJ_LIST_SET:
      return 32;
    case TA_PARAM_POLY_OR_VOL: {
      int type = GetPolyType_raw(pcw);
      return type == 0 || type == 1 || type == 3 ? 32 : 64;
    }
    case TA_PARAM_SPRITE:
      return 32;
    case TA_PARAM_VERTEX: {
      return vertex_type == 0 || vertex_type == 1 || vertex_type == 2 ||
                     vertex_type == 3 || vertex_type == 4 || vertex_type == 7 ||
                     vertex_type == 8 || vertex_type == 9 || vertex_type == 10
                 ? 32
                 : 64;
    }
    default:
      return 0;
  }
}

// See "57.1.1.2 Parameter Combinations" for information on the polygon types.
int GetPolyType_raw(const PCW &pcw) {
  if (pcw.list_type == TA_LIST_OPAQUE_MODVOL ||
      pcw.list_type == TA_LIST_TRANSLUCENT_MODVOL) {
    return 6;
  }

  if (pcw.para_type == TA_PARAM_SPRITE) {
    return 5;
  }

  if (pcw.volume) {
    if (pcw.col_type == 0) {
      return 3;
    }
    if (pcw.col_type == 2) {
      return 4;
    }
    if (pcw.col_type == 3) {
      return 3;
    }
  }

  if (pcw.col_type == 0 || pcw.col_type == 1 || pcw.col_type == 3) {
    return 0;
  }
  if (pcw.col_type == 2 && pcw.texture && !pcw.offset) {
    return 1;
  }
  if (pcw.col_type == 2 && pcw.texture && pcw.offset) {
    return 2;
  }
  if (pcw.col_type == 2 && !pcw.texture) {
    return 1;
  }

  return 0;
}

// See "57.1.1.2 Parameter Combinations" for information on the vertex types.
static int GetVertexType_raw(const PCW &pcw) {
  if (pcw.list_type == TA_LIST_OPAQUE_MODVOL ||
      pcw.list_type == TA_LIST_TRANSLUCENT_MODVOL) {
    return 17;
  }

  if (pcw.para_type == TA_PARAM_SPRITE) {
    return pcw.texture ? 16 : 15;
  }

  if (pcw.volume) {
    if (pcw.texture) {
      if (pcw.col_type == 0) {
        return pcw.uv_16bit ? 12 : 11;
      }
      if (pcw.col_type == 2 || pcw.col_type == 3) {
        return pcw.uv_16bit ? 14 : 13;
      }
    }

    if (pcw.col_type == 0) {
      return 9;
    }
    if (pcw.col_type == 2 || pcw.col_type == 3) {
      return 10;
    }
  }

  if (pcw.texture) {
    if (pcw.col_type == 0) {
      return pcw.uv_16bit ? 4 : 3;
    }
    if (pcw.col_type == 1) {
      return pcw.uv_16bit ? 6 : 5;
    }
    if (pcw.col_type == 2 || pcw.col_type == 3) {
      return pcw.uv_16bit ? 8 : 7;
    }
  }

  if (pcw.col_type == 0) {
    return 0;
  }
  if (pcw.col_type == 1) {
    return 1;
  }
  if (pcw.col_type == 2 || pcw.col_type == 3) {
    return 2;
  }

  return 0;
}

int TileAccelerator::GetParamSize(const PCW &pcw, int vertex_type) {
  int size =
      param_size_lookup[pcw.obj_control * TA_NUM_PARAMS * TA_NUM_VERT_TYPES +
                        pcw.para_type * TA_NUM_VERT_TYPES + vertex_type];
  CHECK_NE(size, 0);
  return size;
}

int TileAccelerator::GetPolyType(const PCW &pcw) {
  return poly_type_lookup[pcw.obj_control * TA_NUM_PARAMS * TA_NUM_LISTS +
                          pcw.para_type * TA_NUM_LISTS + pcw.list_type];
}

int TileAccelerator::GetVertexType(const PCW &pcw) {
  return vertex_type_lookup[pcw.obj_control * TA_NUM_PARAMS * TA_NUM_LISTS +
                            pcw.para_type * TA_NUM_LISTS + pcw.list_type];
}

TileAccelerator::TileAccelerator(Dreamcast *dc, Backend *rb)
    : Device(*dc),
      MemoryInterface(this),
      WindowInterface(this),
      dc_(dc),
      rb_(rb),
      tile_renderer_(*rb, *this),
      trace_writer_(nullptr),
      num_invalidated_(0),
      contexts_() {
  // initialize context queue
  for (int i = 0; i < MAX_CONTEXTS; i++) {
    free_contexts_.push(&contexts_[i]);
  }
}

bool TileAccelerator::Init() {
  memory_ = dc_->memory;
  holly_ = dc_->holly;
  video_ram_ = dc_->memory->TranslateVirtual(PVR_VRAM32_START);

  return true;
}

TextureHandle TileAccelerator::GetTexture(const TSP &tsp, const TCW &tcw,
                                          RegisterTextureCallback register_cb) {
  // if there are any pending removals, do so at this time
  if (pending_invalidations_.size()) {
    ClearPendingTextures();
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
  uint8_t *video_ram = dc_->memory->TranslateVirtual(PVR_VRAM32_START);
  uint8_t *texture = &video_ram[texture_addr];
  int width = 8 << tsp.texture_u_size;
  int height = 8 << tsp.texture_v_size;
  int element_size_bits = tcw.pixel_format == TA_PIXEL_8BPP
                              ? 8
                              : tcw.pixel_format == TA_PIXEL_4BPP ? 4 : 16;
  int texture_size = (width * height * element_size_bits) >> 3;

  // get the palette data
  uint8_t *palette_ram = dc_->memory->TranslateVirtual(PVR_PALETTE_START);
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

  entry.texture_watch = AddSingleWriteWatch(
      texture, texture_size, &HandleTextureWrite, this, map_entry);

  if (palette) {
    entry.palette_watch = AddSingleWriteWatch(
        palette, palette_size, &HandlePaletteWrite, this, map_entry);
  }

  if (trace_writer_) {
    trace_writer_->WriteInsertTexture(tsp, tcw, palette, palette_size, texture,
                                      texture_size);
  }

  return handle;
}

void TileAccelerator::SoftReset() {
  // FIXME what are we supposed to do here?
}

void TileAccelerator::InitContext(uint32_t addr) {
  // try to reuse an existing live context
  auto it = live_contexts_.find(addr);

  if (it == live_contexts_.end()) {
    CHECK(free_contexts_.size());

    TileContext *tactx = free_contexts_.front();
    free_contexts_.pop();

    auto res = live_contexts_.insert(std::make_pair(addr, tactx));
    CHECK(res.second);
    it = res.first;
  }

  TileContext *tactx = it->second;
  memset(tactx, 0, sizeof(*tactx));
  tactx->addr = addr;
  tactx->cursor = 0;
  tactx->size = 0;
  tactx->last_poly = nullptr;
  tactx->last_vertex = nullptr;
  tactx->list_type = 0;
  tactx->vertex_type = 0;
}

void TileAccelerator::WriteContext(uint32_t addr, uint32_t value) {
  auto it = live_contexts_.find(addr);
  CHECK_NE(it, live_contexts_.end());
  TileContext *tactx = it->second;

  CHECK_LT(tactx->size + 4, (int)sizeof(tactx->data));
  *(uint32_t *)&tactx->data[tactx->size] = value;
  tactx->size += 4;

  // each TA command is either 32 or 64 bytes, with the PCW being in the first
  // 32 bytes always. check every 32 bytes to see if the command has been
  // completely received or not
  if (tactx->size % 32 == 0) {
    void *data = &tactx->data[tactx->cursor];
    PCW pcw = re::load<PCW>(data);

    int size = GetParamSize(pcw, tactx->vertex_type);
    int recv = tactx->size - tactx->cursor;

    if (recv < size) {
      // wait for the entire command
      return;
    }

    if (pcw.para_type == TA_PARAM_END_OF_LIST) {
      holly_->RequestInterrupt(list_interrupts[tactx->list_type]);

      tactx->last_poly = nullptr;
      tactx->last_vertex = nullptr;
      tactx->list_type = 0;
      tactx->vertex_type = 0;
    } else if (pcw.para_type == TA_PARAM_OBJ_LIST_SET) {
      LOG_FATAL("TA_PARAM_OBJ_LIST_SET unsupported");
    } else if (pcw.para_type == TA_PARAM_POLY_OR_VOL) {
      tactx->last_poly = reinterpret_cast<PolyParam *>(data);
      tactx->last_vertex = nullptr;
      tactx->list_type = tactx->last_poly->type0.pcw.list_type;
      tactx->vertex_type = GetVertexType(tactx->last_poly->type0.pcw);
    } else if (pcw.para_type == TA_PARAM_SPRITE) {
      tactx->last_poly = reinterpret_cast<PolyParam *>(data);
      tactx->last_vertex = nullptr;
      tactx->list_type = tactx->last_poly->type0.pcw.list_type;
      tactx->vertex_type = GetVertexType(tactx->last_poly->type0.pcw);
    }

    tactx->cursor += recv;
  }
}

void TileAccelerator::FinalizeContext(uint32_t addr) {
  auto it = live_contexts_.find(addr);
  CHECK_NE(it, live_contexts_.end());
  TileContext *tactx = it->second;

  // save required register state being that the actual rendering of this
  // context will be deferred
  SaveRegisterState(tactx);

  // tell holly that rendering is complete
  holly_->RequestInterrupt(HOLLY_INTC_PCEOVINT);
  holly_->RequestInterrupt(HOLLY_INTC_PCEOIINT);
  holly_->RequestInterrupt(HOLLY_INTC_PCEOTINT);

  // erase from the live map
  live_contexts_.erase(it);

  // append to the pending queue
  pending_contexts_.push(tactx);

  if (trace_writer_) {
    trace_writer_->WriteRenderContext(tactx);
  }
}

TileContext *TileAccelerator::GetLastContext() {
  if (pending_contexts_.empty()) {
    return nullptr;
  }

  // free pending contexts which are not the latest
  while (pending_contexts_.size() > 1) {
    TileContext *tactx = pending_contexts_.front();
    pending_contexts_.pop();
    free_contexts_.push(tactx);
  }

  // return the latest context
  return pending_contexts_.front();
}

void TileAccelerator::MapPhysicalMemory(Memory &memory, MemoryMap &memmap) {
  RegionHandle ta_poly_handle = memory.AllocRegion(
      TA_POLY_START, TA_POLY_SIZE, nullptr, nullptr, nullptr, nullptr, nullptr,
      nullptr, make_delegate(&TileAccelerator::WritePolyFIFO, this), nullptr);

  RegionHandle ta_texture_handle = memory.AllocRegion(
      TA_TEXTURE_START, TA_TEXTURE_SIZE, nullptr, nullptr, nullptr, nullptr,
      nullptr, nullptr, make_delegate(&TileAccelerator::WriteTextureFIFO, this),
      nullptr);

  memmap.Mount(ta_poly_handle, TA_POLY_SIZE, TA_POLY_START);
  memmap.Mount(ta_texture_handle, TA_TEXTURE_SIZE, TA_TEXTURE_START);
}

void TileAccelerator::OnPaint(bool show_main_menu) {
  // render the latest context
  TileContext *tactx = GetLastContext();

  if (tactx) {
    tile_renderer_.RenderContext(tactx);
  }

  if (show_main_menu && ImGui::BeginMainMenuBar()) {
    if (ImGui::BeginMenu("TA")) {
      if ((!trace_writer_ && ImGui::MenuItem("Start trace")) ||
          (trace_writer_ && ImGui::MenuItem("Stop trace"))) {
        ToggleTracing();
      }
      ImGui::EndMenu();
    }

    ImGui::EndMainMenuBar();
  }
}

void TileAccelerator::ToggleTracing() {
  if (!trace_writer_) {
    char filename[PATH_MAX];
    GetNextTraceFilename(filename, sizeof(filename));

    trace_writer_ = new TraceWriter();

    if (!trace_writer_->Open(filename)) {
      delete trace_writer_;
      trace_writer_ = nullptr;

      LOG_INFO("Failed to start tracing");

      return;
    }

    // clear texture cache in order to generate insert events for all textures
    // referenced while tracing
    ClearTextures();

    LOG_INFO("Begin tracing to %s", filename);
  } else {
    delete trace_writer_;
    trace_writer_ = nullptr;

    LOG_INFO("End tracing");
  }
}

void TileAccelerator::ClearTextures() {
  LOG_INFO("Texture cache cleared");

  auto it = textures_.begin();
  auto e = textures_.end();

  while (it != e) {
    auto curr = it++;
    InvalidateTexture(curr);
  }

  CHECK(!textures_.size());
}

void TileAccelerator::ClearPendingTextures() {
  for (auto texture_key : pending_invalidations_) {
    auto it = textures_.find(texture_key);
    CHECK_NE(it, textures_.end());
    InvalidateTexture(it);
  }

  num_invalidated_ += pending_invalidations_.size();
  PROFILER_COUNT("Num invalidated textures", num_invalidated_);

  pending_invalidations_.clear();
}

void TileAccelerator::InvalidateTexture(TextureKey texture_key) {
  auto it = textures_.find(texture_key);

  // multiple writes may have already invalidated this texture
  if (it == textures_.end()) {
    return;
  }

  InvalidateTexture(it);
}

void TileAccelerator::InvalidateTexture(TextureCacheMap::iterator it) {
  TextureEntry &entry = it->second;

  if (entry.texture_watch) {
    RemoveAccessWatch(entry.texture_watch);
  }

  if (entry.palette_watch) {
    RemoveAccessWatch(entry.palette_watch);
  }

  rb_->FreeTexture(entry.handle);

  textures_.erase(it);
}

void TileAccelerator::HandleTextureWrite(void *ctx, const Exception &ex,
                                         void *data) {
  TileAccelerator *self = reinterpret_cast<TileAccelerator *>(ctx);
  TextureCacheMap::value_type *map_entry =
      reinterpret_cast<TextureCacheMap::value_type *>(data);

  // don't double remove the watch during invalidation
  TextureEntry &entry = map_entry->second;
  entry.texture_watch = nullptr;

  // add to pending invalidation list (can't remove inside of signal
  // handler)
  TextureKey texture_key = map_entry->first;
  self->pending_invalidations_.insert(texture_key);
}

void TileAccelerator::HandlePaletteWrite(void *ctx, const Exception &ex,
                                         void *data) {
  TileAccelerator *self = reinterpret_cast<TileAccelerator *>(ctx);
  TextureCacheMap::value_type *map_entry =
      reinterpret_cast<TextureCacheMap::value_type *>(data);

  // don't double remove the watch during invalidation
  TextureEntry &entry = map_entry->second;
  entry.palette_watch = nullptr;

  // add to pending invalidation list (can't remove inside of signal
  // handler)
  TextureKey texture_key = map_entry->first;
  self->pending_invalidations_.insert(texture_key);
}

void TileAccelerator::WritePolyFIFO(uint32_t addr, uint32_t value) {
  WriteContext(dc_->TA_ISP_BASE.base_address, value);
}

void TileAccelerator::WriteTextureFIFO(uint32_t addr, uint32_t value) {
  addr &= 0xeeffffff;

  re::store(&video_ram_[addr], value);
}

void TileAccelerator::SaveRegisterState(TileContext *tactx) {
  // autosort
  if (!dc_->FPU_PARAM_CFG.region_header_type) {
    tactx->autosort = !dc_->ISP_FEED_CFG.presort;
  } else {
    uint32_t region_data = memory_->R32(PVR_VRAM64_START + dc_->REGION_BASE);
    tactx->autosort = !(region_data & 0x20000000);
  }

  // texture stride
  tactx->stride = dc_->TEXT_CONTROL.stride * 32;

  // texture palette pixel format
  tactx->pal_pxl_format = dc_->PAL_RAM_CTRL.pixel_format;

  // write out video width to help with unprojecting the screen space
  // coordinates
  if (dc_->SPG_CONTROL.interlace ||
      (!dc_->SPG_CONTROL.NTSC && !dc_->SPG_CONTROL.PAL)) {
    // interlaced and VGA mode both render at full resolution
    tactx->video_width = 640;
    tactx->video_height = 480;
  } else {
    tactx->video_width = 320;
    tactx->video_height = 240;
  }

  // according to the hardware docs, this is the correct calculation of the
  // background ISP address. however, in practice, the second TA buffer's ISP
  // address comes out to be 0x800000 when booting the bios and the vram is
  // only 8mb total. by examining a raw memory dump, the ISP data is only ever
  // available at 0x0 when booting the bios, so masking this seems to be the
  // correct solution
  uint32_t vram_offset =
      PVR_VRAM64_START +
      ((tactx->addr + dc_->ISP_BACKGND_T.tag_address * 4) & 0x7fffff);

  // get surface parameters
  tactx->bg_isp.full = memory_->R32(vram_offset);
  tactx->bg_tsp.full = memory_->R32(vram_offset + 4);
  tactx->bg_tcw.full = memory_->R32(vram_offset + 8);
  vram_offset += 12;

  // get the background depth
  tactx->bg_depth = re::load<float>(&dc_->ISP_BACKGND_D);

  // get the byte size for each vertex. normally, the byte size is
  // ISP_BACKGND_T.skip + 3, but if parameter selection volume mode is in
  // effect and the shadow bit is 1, then the byte size is
  // ISP_BACKGND_T.skip * 2 + 3
  int vertex_size = dc_->ISP_BACKGND_T.skip;
  if (!dc_->FPU_SHAD_SCALE.intensity_volume_mode && dc_->ISP_BACKGND_T.shadow) {
    vertex_size *= 2;
  }
  vertex_size = (vertex_size + 3) * 4;

  // skip to the first vertex
  vram_offset += dc_->ISP_BACKGND_T.tag_offset * vertex_size;

  // copy vertex data to context
  for (int i = 0, bg_offset = 0; i < 3; i++) {
    CHECK_LE(bg_offset + vertex_size, (int)sizeof(tactx->bg_vertices));

    memory_->Memcpy(&tactx->bg_vertices[bg_offset], vram_offset, vertex_size);

    bg_offset += vertex_size;
    vram_offset += vertex_size;
  }
}
