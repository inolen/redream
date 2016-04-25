#include "core/memory.h"
#include "core/profiler.h"
#include "hw/holly/holly.h"
#include "hw/holly/pixel_convert.h"
#include "hw/holly/tile_accelerator.h"
#include "hw/holly/pvr2.h"
#include "hw/holly/trace.h"
#include "hw/sh4/sh4.h"
#include "hw/dreamcast.h"
#include "hw/memory.h"
#include "sys/filesystem.h"

using namespace re;
using namespace re::hw;
using namespace re::hw::holly;
using namespace re::renderer;
using namespace re::sys;

// clang-format off
AM_BEGIN(TileAccelerator, fifo_map)
  AM_RANGE(0x0000000, 0x07fffff) AM_HANDLE(nullptr,
                                           nullptr,
                                           nullptr,
                                           nullptr,
                                           nullptr,
                                           nullptr,
                                           &TileAccelerator::WritePolyFIFO,
                                           nullptr)
  AM_RANGE(0x1000000, 0x1ffffff) AM_HANDLE(nullptr,
                                           nullptr,
                                           nullptr,
                                           nullptr,
                                           nullptr,
                                           nullptr,
                                           &TileAccelerator::WriteTextureFIFO,
                                           nullptr)
AM_END()
    // clang-format on

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

TileAccelerator::TileAccelerator(Dreamcast &dc, Backend *rb)
    : Device(dc, "ta"),
      WindowInterface(this),
      dc_(dc),
      rb_(rb),
      tile_renderer_(rb, *this),
      sh4_(nullptr),
      holly_(nullptr),
      pvr_(nullptr),
      video_ram_(nullptr),
      trace_writer_(nullptr),
      num_invalidated_(0),
      tctxs_(),
      last_tctx_(nullptr) {
  // initialize context queue
  for (int i = 0; i < MAX_CONTEXTS; i++) {
    free_tctxs_.push(&tctxs_[i]);
  }
}

bool TileAccelerator::Init() {
  sh4_ = dc_.sh4();
  holly_ = dc_.holly();
  pvr_ = dc_.pvr();
  video_ram_ = sh4_->space().Translate(0x04000000);

// initialize registers
#define TA_REG_R32(name)          \
  pvr_->reg(name##_OFFSET).read = \
      make_delegate(&TileAccelerator::name##_r, this)
#define TA_REG_W32(name)           \
  pvr_->reg(name##_OFFSET).write = \
      make_delegate(&TileAccelerator::name##_w, this)
  TA_REG_W32(SOFTRESET);
  TA_REG_W32(TA_LIST_INIT);
  TA_REG_W32(TA_LIST_CONT);
  TA_REG_W32(STARTRENDER);
#undef TA_REG_R32
#undef TA_REG_W32

  return true;
}

TextureHandle TileAccelerator::GetTexture(
    const TileContext &tctx, const TSP &tsp, const TCW &tcw,
    RegisterTextureDelegate register_delegate) {
  // if there are any pending removals, do so at this time
  if (pending_invalidations_.size()) {
    ClearPendingTextures();
  }

  // TODO TileContext isn't considered for caching here (stride and
  // pal_pxl_format are used by TileRenderer), this feels bad

  // see if an an entry already exists
  TextureKey texture_key = TextureProvider::GetTextureKey(tsp, tcw);

  auto it = textures_.find(texture_key);
  if (it != textures_.end()) {
    return it->second.handle;
  }

  // TCW texture_addr field is in 64-bit units
  uint32_t texture_addr = tcw.texture_addr << 3;

  // get the texture data
  uint8_t *video_ram = sh4_->space().Translate(0x04000000);
  uint8_t *texture = &video_ram[texture_addr];
  int width = 8 << tsp.texture_u_size;
  int height = 8 << tsp.texture_v_size;
  int element_size_bits = tcw.pixel_format == TA_PIXEL_8BPP
                              ? 8
                              : tcw.pixel_format == TA_PIXEL_4BPP ? 4 : 16;
  int texture_size = (width * height * element_size_bits) >> 3;

  // get the palette data
  uint8_t *palette_ram = sh4_->space().Translate(0x005f9000);
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
  RegisterTextureResult reg =
      register_delegate(tctx, tsp, tcw, palette, texture);
  auto result =
      textures_.insert(std::make_pair(texture_key, TextureEntry(reg.handle)));
  CHECK(result.second, "Texture already in the map?");

  // add write callback in order to invalidate on future writes. the callback
  // address will be page aligned, therefore it will be triggered falsely in
  // some cases. over invalidate in these cases
  TextureEntry &entry = result.first->second;
  TextureCacheMap::value_type *map_entry = &(*result.first);

  entry.texture_watch = AddSingleWriteWatch(
      texture, texture_size,
      make_delegate(&TileAccelerator::HandleTextureWrite, this), map_entry);

  if (palette) {
    entry.palette_watch = AddSingleWriteWatch(
        palette, palette_size,
        make_delegate(&TileAccelerator::HandlePaletteWrite, this), map_entry);
  }

  if (trace_writer_) {
    trace_writer_->WriteInsertTexture(tsp, tcw, palette, palette_size, texture,
                                      texture_size);
  }

  return reg.handle;
}

void TileAccelerator::SoftReset() {
  // FIXME what are we supposed to do here?
}

void TileAccelerator::InitContext(uint32_t addr) {
  // try to reuse an existing live context
  auto it = live_tctxs_.find(addr);

  if (it == live_tctxs_.end()) {
    CHECK(free_tctxs_.size());

    TileContext *tctx = free_tctxs_.front();
    CHECK_NOTNULL(tctx);
    free_tctxs_.pop();

    auto res = live_tctxs_.insert(std::make_pair(addr, tctx));
    CHECK(res.second);
    it = res.first;
  }

  TileContext *tctx = it->second;
  memset(tctx, 0, sizeof(*tctx));
  tctx->addr = addr;
  tctx->cursor = 0;
  tctx->size = 0;
  tctx->last_poly = nullptr;
  tctx->last_vertex = nullptr;
  tctx->list_type = 0;
  tctx->vertex_type = 0;
}

void TileAccelerator::WriteContext(uint32_t addr, uint32_t value) {
  auto it = live_tctxs_.find(addr);
  CHECK_NE(it, live_tctxs_.end());
  TileContext *tctx = it->second;

  CHECK_LT(tctx->size + 4, (int)sizeof(tctx->data));
  *(uint32_t *)&tctx->data[tctx->size] = value;
  tctx->size += 4;

  // each TA command is either 32 or 64 bytes, with the PCW being in the first
  // 32 bytes always. check every 32 bytes to see if the command has been
  // completely received or not
  if (tctx->size % 32 == 0) {
    void *data = &tctx->data[tctx->cursor];
    PCW pcw = re::load<PCW>(data);

    int size = GetParamSize(pcw, tctx->vertex_type);
    int recv = tctx->size - tctx->cursor;

    if (recv < size) {
      // wait for the entire command
      return;
    }

    if (pcw.para_type == TA_PARAM_END_OF_LIST) {
      holly_->RequestInterrupt(list_interrupts[tctx->list_type]);

      tctx->last_poly = nullptr;
      tctx->last_vertex = nullptr;
      tctx->list_type = 0;
      tctx->vertex_type = 0;
    } else if (pcw.para_type == TA_PARAM_OBJ_LIST_SET) {
      LOG_FATAL("TA_PARAM_OBJ_LIST_SET unsupported");
    } else if (pcw.para_type == TA_PARAM_POLY_OR_VOL) {
      tctx->last_poly = reinterpret_cast<PolyParam *>(data);
      tctx->last_vertex = nullptr;
      tctx->list_type = tctx->last_poly->type0.pcw.list_type;
      tctx->vertex_type = GetVertexType(tctx->last_poly->type0.pcw);
    } else if (pcw.para_type == TA_PARAM_SPRITE) {
      tctx->last_poly = reinterpret_cast<PolyParam *>(data);
      tctx->last_vertex = nullptr;
      tctx->list_type = tctx->last_poly->type0.pcw.list_type;
      tctx->vertex_type = GetVertexType(tctx->last_poly->type0.pcw);
    }

    tctx->cursor += recv;
  }
}

void TileAccelerator::FinalizeContext(uint32_t addr) {
  auto it = live_tctxs_.find(addr);
  CHECK_NE(it, live_tctxs_.end());
  TileContext *tctx = it->second;

  // save required register state being that the actual rendering of this
  // context will be deferred
  SaveRegisterState(tctx);

  // tell holly that rendering is complete
  holly_->RequestInterrupt(HOLLY_INTC_PCEOVINT);
  holly_->RequestInterrupt(HOLLY_INTC_PCEOIINT);
  holly_->RequestInterrupt(HOLLY_INTC_PCEOTINT);

  // erase from the live map
  live_tctxs_.erase(it);

  // free and replace the last context
  if (last_tctx_) {
    free_tctxs_.push(last_tctx_);
    last_tctx_ = nullptr;
  }

  last_tctx_ = tctx;
}

void TileAccelerator::WritePolyFIFO(uint32_t addr, uint32_t value) {
  WriteContext(pvr_->TA_ISP_BASE.base_address, value);
}

void TileAccelerator::WriteTextureFIFO(uint32_t addr, uint32_t value) {
  addr &= 0xeeffffff;
  re::store(&video_ram_[addr], value);
}

void TileAccelerator::OnPaint(bool show_main_menu) {
  if (last_tctx_) {
    tile_renderer_.RenderContext(*last_tctx_);

    // write render command after actually rendering the context so texture
    // insert commands will be written out first
    if (trace_writer_ && !last_tctx_->wrote) {
      trace_writer_->WriteRenderContext(last_tctx_);

      last_tctx_->wrote = true;
    }
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

void TileAccelerator::HandleTextureWrite(const Exception &ex, void *data) {
  TextureCacheMap::value_type *map_entry =
      reinterpret_cast<TextureCacheMap::value_type *>(data);

  // don't double remove the watch during invalidation
  TextureEntry &entry = map_entry->second;
  entry.texture_watch = nullptr;

  // add to pending invalidation list (can't remove inside of signal
  // handler)
  TextureKey texture_key = map_entry->first;
  pending_invalidations_.insert(texture_key);
}

void TileAccelerator::HandlePaletteWrite(const Exception &ex, void *data) {
  TextureCacheMap::value_type *map_entry =
      reinterpret_cast<TextureCacheMap::value_type *>(data);

  // don't double remove the watch during invalidation
  TextureEntry &entry = map_entry->second;
  entry.palette_watch = nullptr;

  // add to pending invalidation list (can't remove inside of signal
  // handler)
  TextureKey texture_key = map_entry->first;
  pending_invalidations_.insert(texture_key);
}

void TileAccelerator::SaveRegisterState(TileContext *tctx) {
  // autosort
  if (!pvr_->FPU_PARAM_CFG.region_header_type) {
    tctx->autosort = !pvr_->ISP_FEED_CFG.presort;
  } else {
    uint32_t region_data = sh4_->space().R32(0x05000000 + pvr_->REGION_BASE);
    tctx->autosort = !(region_data & 0x20000000);
  }

  // texture stride
  tctx->stride = pvr_->TEXT_CONTROL.stride * 32;

  // texture palette pixel format
  tctx->pal_pxl_format = pvr_->PAL_RAM_CTRL.pixel_format;

  // write out video width to help with unprojecting the screen space
  // coordinates
  if (pvr_->SPG_CONTROL.interlace ||
      (!pvr_->SPG_CONTROL.NTSC && !pvr_->SPG_CONTROL.PAL)) {
    // interlaced and VGA mode both render at full resolution
    tctx->video_width = 640;
    tctx->video_height = 480;
  } else {
    tctx->video_width = 320;
    tctx->video_height = 240;
  }

  // according to the hardware docs, this is the correct calculation of the
  // background ISP address. however, in practice, the second TA buffer's ISP
  // address comes out to be 0x800000 when booting the bios and the vram is
  // only 8mb total. by examining a raw memory dump, the ISP data is only ever
  // available at 0x0 when booting the bios, so masking this seems to be the
  // correct solution
  uint32_t vram_offset =
      0x05000000 +
      ((tctx->addr + pvr_->ISP_BACKGND_T.tag_address * 4) & 0x7fffff);

  // get surface parameters
  tctx->bg_isp.full = sh4_->space().R32(vram_offset);
  tctx->bg_tsp.full = sh4_->space().R32(vram_offset + 4);
  tctx->bg_tcw.full = sh4_->space().R32(vram_offset + 8);
  vram_offset += 12;

  // get the background depth
  tctx->bg_depth = re::load<float>(&pvr_->ISP_BACKGND_D);

  // get the byte size for each vertex. normally, the byte size is
  // ISP_BACKGND_T.skip + 3, but if parameter selection volume mode is in
  // effect and the shadow bit is 1, then the byte size is
  // ISP_BACKGND_T.skip * 2 + 3
  int vertex_size = pvr_->ISP_BACKGND_T.skip;
  if (!pvr_->FPU_SHAD_SCALE.intensity_volume_mode &&
      pvr_->ISP_BACKGND_T.shadow) {
    vertex_size *= 2;
  }
  vertex_size = (vertex_size + 3) * 4;

  // skip to the first vertex
  vram_offset += pvr_->ISP_BACKGND_T.tag_offset * vertex_size;

  // copy vertex data to context
  for (int i = 0, bg_offset = 0; i < 3; i++) {
    CHECK_LE(bg_offset + vertex_size, (int)sizeof(tctx->bg_vertices));

    sh4_->space().Memcpy(&tctx->bg_vertices[bg_offset], vram_offset,
                         vertex_size);

    bg_offset += vertex_size;
    vram_offset += vertex_size;
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

W32_DELEGATE(TileAccelerator::SOFTRESET) {
  if (!(reg.value & 0x1)) {
    return;
  }

  SoftReset();
}

W32_DELEGATE(TileAccelerator::TA_LIST_INIT) {
  if (!(reg.value & 0x80000000)) {
    return;
  }

  InitContext(pvr_->TA_ISP_BASE.base_address);
}

W32_DELEGATE(TileAccelerator::TA_LIST_CONT) {
  if (!(reg.value & 0x80000000)) {
    return;
  }

  LOG_WARNING("Unsupported TA_LIST_CONT");
}

W32_DELEGATE(TileAccelerator::STARTRENDER) {
  if (!reg.value) {
    return;
  }

  FinalizeContext(pvr_->PARAM_BASE.base_address);
}
