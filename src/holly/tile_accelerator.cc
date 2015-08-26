#include "core/core.h"
#include "holly/holly.h"
#include "holly/pixel_convert.h"
#include "holly/pvr2.h"
#include "holly/tile_accelerator.h"
#include "trace/trace.h"

using namespace dreavm::core;
using namespace dreavm::emu;
using namespace dreavm::holly;
using namespace dreavm::renderer;
using namespace dreavm::trace;

static void BuildLookupTables();
static size_t GetParamSize_raw(const PCW &pcw, int vertex_type);
static int GetPolyType_raw(const PCW &pcw);
static int GetVertexType_raw(const PCW &pcw);

static HollyInterrupt list_interrupts[] = {
    HOLLY_INTC_TAEOINT,   // TA_LIST_OPAQUE
    HOLLY_INTC_TAEOMINT,  // TA_LIST_OPAQUE_MODVOL
    HOLLY_INTC_TAETINT,   // TA_LIST_TRANSLUCENT
    HOLLY_INTC_TAETMINT,  // TA_LIST_TRANSLUCENT_MODVOL
    HOLLY_INTC_TAEPTIN    // TA_LIST_PUNCH_THROUGH
};

static size_t param_size_lookup[0x100 * TA_NUM_PARAMS * TA_NUM_VERT_TYPES];
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
static size_t GetParamSize_raw(const PCW &pcw, int vertex_type) {
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
  } else if (pcw.para_type == TA_PARAM_SPRITE) {
    return 5;
  } else if (pcw.volume) {
    if (pcw.col_type == 0) {
      return 3;
    } else if (pcw.col_type == 2) {
      return 4;
    } else if (pcw.col_type == 3) {
      return 3;
    }
  } else {
    if (pcw.col_type == 0 || pcw.col_type == 1 || pcw.col_type == 3) {
      return 0;
    } else if (pcw.col_type == 2 && pcw.texture && !pcw.offset) {
      return 1;
    } else if (pcw.col_type == 2 && pcw.texture && pcw.offset) {
      return 2;
    } else if (pcw.col_type == 2 && !pcw.texture) {
      return 1;
    }
  }

  return 0;
}

// See "57.1.1.2 Parameter Combinations" for information on the vertex types.
static int GetVertexType_raw(const PCW &pcw) {
  if (pcw.list_type == TA_LIST_OPAQUE_MODVOL ||
      pcw.list_type == TA_LIST_TRANSLUCENT_MODVOL) {
    return 17;
  } else if (pcw.para_type == TA_PARAM_SPRITE) {
    return pcw.texture ? 16 : 15;
  } else if (pcw.volume) {
    if (pcw.texture) {
      if (pcw.col_type == 0) {
        return pcw.uv_16bit ? 12 : 11;
      } else if (pcw.col_type == 2 || pcw.col_type == 3) {
        return pcw.uv_16bit ? 14 : 13;
      }
    } else {
      if (pcw.col_type == 0) {
        return 9;
      } else if (pcw.col_type == 2 || pcw.col_type == 3) {
        return 10;
      }
    }
  } else {
    if (pcw.texture) {
      if (pcw.col_type == 0) {
        return pcw.uv_16bit ? 4 : 3;
      } else if (pcw.col_type == 1) {
        return pcw.uv_16bit ? 6 : 5;
      } else if (pcw.col_type == 2 || pcw.col_type == 3) {
        return pcw.uv_16bit ? 8 : 7;
      }
    } else {
      if (pcw.col_type == 0) {
        return 0;
      } else if (pcw.col_type == 1) {
        return 1;
      } else if (pcw.col_type == 2 || pcw.col_type == 3) {
        return 2;
      }
    }
  }

  return 0;
}

TileTextureCache::TileTextureCache(TileAccelerator &ta) : ta_(ta) {}

void TileTextureCache::Clear() {
  for (auto it : textures_) {
    if (!it.second) {
      continue;
    }

    ta_.rb_->FreeTexture(it.second);
  }

  textures_.clear();
}

void TileTextureCache::RemoveTexture(uint32_t addr) {
  auto it = textures_.find(addr);
  if (it == textures_.end()) {
    return;
  }

  TextureHandle handle = it->second;
  ta_.rb_->FreeTexture(handle);
  textures_.erase(it);
}

TextureHandle TileTextureCache::GetTexture(
    const TSP &tsp, const TCW &tcw, RegisterTextureCallback register_cb) {
  uint32_t texture_key = TextureCache::GetTextureKey(tsp, tcw);

  // see if we already have an entry
  auto it = textures_.find(texture_key);
  if (it != textures_.end()) {
    return it->second;
  }

  // TCW texture_addr field is in 64-bit units
  uint32_t texture_addr = tcw.texture_addr << 3;

  // get the texture data
  int width = 8 << tsp.texture_u_size;
  int height = 8 << tsp.texture_v_size;
  int element_size_bits = tcw.pixel_format == TA_PIXEL_8BPP
                              ? 8
                              : tcw.pixel_format == TA_PIXEL_4BPP ? 4 : 16;
  int texture_size = (width * height * element_size_bits) >> 3;
  const uint8_t *texture = &ta_.pvr_.vram_[texture_addr];

  // get the palette data
  int palette_size = 0;
  const uint8_t *palette = nullptr;

  if (tcw.pixel_format == TA_PIXEL_4BPP || tcw.pixel_format == TA_PIXEL_8BPP) {
    uint32_t palette_addr = 0;

    if (tcw.pixel_format == TA_PIXEL_4BPP) {
      palette_addr = (tcw.p.palette_selector << 4);
    } else {
      // in 8BPP palette mode, only the upper two bits are valid
      palette_addr = ((tcw.p.palette_selector & 0x30) << 4);
    }

    palette_size = 0x1000;
    palette = &ta_.pvr_.pram_[palette_addr];
  }

  // register and insert into the cache
  TextureHandle handle = register_cb(texture, palette);
  auto result = textures_.insert(std::make_pair(texture_key, handle));
  CHECK(result.second, "Texture already in the map?");

  // add insert to trace
  if (ta_.trace_writer_) {
    ta_.trace_writer_->WriteInsertTexture(tsp, tcw, texture, texture_size,
                                          palette, palette_size);
  }

  return result.first->second;
}

size_t TileAccelerator::GetParamSize(const PCW &pcw, int vertex_type) {
  size_t size =
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

TileAccelerator::TileAccelerator(Memory &memory, Holly &holly, PVR2 &pvr)
    : memory_(memory),
      holly_(holly),
      pvr_(pvr),
      texcache_(*this),
      renderer_(texcache_) {}

TileAccelerator::~TileAccelerator() {
  while (contexts_.size()) {
    const auto &it = contexts_.begin();

    TileContext *tactx = it->second;
    delete tactx;

    contexts_.erase(it);
  }
}

bool TileAccelerator::Init(Backend *rb) {
  rb_ = rb;

  InitMemory();

  return true;
}

void TileAccelerator::ResizeVideo(int width, int height) {
  rb_->SetFramebufferSize(FB_TILE_ACCELERATOR, width, height);

  if (trace_writer_) {
    trace_writer_->WriteResizeVideo(width, height);
  }
}

void TileAccelerator::SoftReset() {
  // FIXME what are we supposed to do here?
}

void TileAccelerator::InitContext(uint32_t addr) {
  TileContext *tactx = GetContext(addr);

  tactx->cursor = 0;
  tactx->size = 0;
  tactx->last_poly = nullptr;
  tactx->last_vertex = nullptr;
  tactx->list_type = 0;
  tactx->vertex_type = 0;
}

void TileAccelerator::WriteContext(uint32_t addr, uint32_t value) {
  TileContext *tactx = GetContext(addr);

  CHECK_LT(tactx->size + 4, (int)sizeof(tactx->data));
  *(uint32_t *)&tactx->data[tactx->size] = value;
  tactx->size += 4;

  // each TA command is either 32 or 64 bytes, with the PCW being in the first
  // 32 bytes always. check every 32 bytes to see if the command has been
  // completely received or not
  if (tactx->size % 32 == 0) {
    void *data = &tactx->data[tactx->cursor];
    PCW pcw = *reinterpret_cast<PCW *>(data);

    size_t size = GetParamSize(pcw, tactx->vertex_type);
    size_t recv = tactx->size - tactx->cursor;

    if (recv < size) {
      // wait for the entire command
      return;
    }

    if (pcw.para_type == TA_PARAM_END_OF_LIST) {
      holly_.RequestInterrupt(list_interrupts[tactx->list_type]);

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

void TileAccelerator::RenderContext(uint32_t addr) {
  // get context and update with PVR state
  TileContext *tactx = GetContext(addr);

  WritePVRState(tactx);
  WriteBackgroundState(tactx);

  // do the actual rendering
  renderer_.RenderContext(tactx, rb_);

  // let holly know the rendering is complete
  holly_.RequestInterrupt(HOLLY_INTC_PCEOVINT);
  holly_.RequestInterrupt(HOLLY_INTC_PCEOIINT);
  holly_.RequestInterrupt(HOLLY_INTC_PCEOTINT);

  // add render to trace
  if (trace_writer_) {
    trace_writer_->WriteRenderContext(tactx);
  }
}

void TileAccelerator::ToggleTracing() {
  if (!trace_writer_) {
    char filename[PATH_MAX];
    GetNextTraceFilename(filename, sizeof(filename));

    trace_writer_ = std::unique_ptr<TraceWriter>(new TraceWriter());
    if (!trace_writer_->Open(filename)) {
      LOG_INFO("Failed to start tracing");
      trace_writer_ = nullptr;
      return;
    }

    LOG_INFO("Begin tracing to %s", filename);

    // write out the initial framebuffer size
    int width, height;
    rb_->GetFramebufferSize(FB_TILE_ACCELERATOR, &width, &height);
    trace_writer_->WriteResizeVideo(width, height);

    // clear the texture cache, so the next render will write out insert
    // texture commands for any textures in use
    texcache_.Clear();
  } else {
    trace_writer_ = nullptr;

    LOG_INFO("End tracing");
  }
}

namespace dreavm {
namespace holly {

template <typename T>
void TileAccelerator::WriteCommand(void *ctx, uint32_t addr, T value) {
  WriteCommand<uint32_t>(ctx, addr, static_cast<uint32_t>(value));
}

template <>
void TileAccelerator::WriteCommand(void *ctx, uint32_t addr, uint32_t value) {
  TileAccelerator *ta = (TileAccelerator *)ctx;

  ta->WriteContext(ta->pvr_.TA_ISP_BASE.base_address, value);
}

template <typename T>
void TileAccelerator::WriteTexture(void *ctx, uint32_t addr, T value) {
  WriteTexture<uint32_t>(ctx, addr, static_cast<uint32_t>(value));
}

template <>
void TileAccelerator::WriteTexture(void *ctx, uint32_t addr, uint32_t value) {
  TileAccelerator *ta = (TileAccelerator *)ctx;

  addr &= 0xeeffffff;

  // FIXME this is terrible
  ta->texcache_.RemoveTexture(addr);

  *reinterpret_cast<uint32_t *>(&ta->pvr_.vram_[addr]) = value;
}
}
}

void TileAccelerator::InitMemory() {
  // TODO handle YUV transfers from 0x10800000 - 0x10ffffe0
  memory_.Handle(TA_CMD_START, TA_CMD_END, 0x0, this, nullptr, nullptr, nullptr,
                 nullptr, &TileAccelerator::WriteCommand<uint8_t>,
                 &TileAccelerator::WriteCommand<uint16_t>,
                 &TileAccelerator::WriteCommand<uint32_t>, nullptr);
  memory_.Handle(TA_TEXTURE_START, TA_TEXTURE_END, 0x0, this, nullptr, nullptr,
                 nullptr, nullptr, &TileAccelerator::WriteTexture<uint8_t>,
                 &TileAccelerator::WriteTexture<uint16_t>,
                 &TileAccelerator::WriteTexture<uint32_t>, nullptr);
}

TileContext *TileAccelerator::GetContext(uint32_t addr) {
  auto it = contexts_.find(addr);

  if (it != contexts_.end()) {
    return it->second;
  }

  TileContext *ctx = new TileContext();
  ctx->addr = addr;

  auto result = contexts_.insert(std::make_pair(addr, ctx));
  return result.first->second;
}

void TileAccelerator::WritePVRState(TileContext *tactx) {
  // autosort
  if (!pvr_.FPU_PARAM_CFG.region_header_type) {
    tactx->autosort = !pvr_.ISP_FEED_CFG.presort;
  } else {
    uint32_t region_data = memory_.R32(PVR_VRAM64_START + pvr_.REGION_BASE);
    tactx->autosort = !(region_data & 0x20000000);
  }

  // texture stride
  tactx->stride = pvr_.TEXT_CONTROL.stride * 32;

  // texture palette pixel format
  tactx->pal_pxl_format = pvr_.PAL_RAM_CTRL.pixel_format;
}

void TileAccelerator::WriteBackgroundState(TileContext *tactx) {
  // according to the hardware docs, this is the correct calculation of the
  // background ISP address. however, in practice, the second TA buffer's ISP
  // address comes out to be 0x800000 when booting the the bios when the vram
  // is only 8mb total. by examining a raw memory dump, the ISP data is only
  // ever available at 0x0 when booting the bios, so masking this seems to
  // be the correct solution
  uint32_t vram_offset =
      PVR_VRAM64_START +
      ((tactx->addr + pvr_.ISP_BACKGND_T.tag_address * 4) & 0x7fffff);

  // get surface parameters
  tactx->bg_isp.full = memory_.R32(vram_offset);
  tactx->bg_tsp.full = memory_.R32(vram_offset + 4);
  tactx->bg_tcw.full = memory_.R32(vram_offset + 8);
  vram_offset += 12;

  // get the background depth
  tactx->bg_depth = *reinterpret_cast<float *>(&pvr_.ISP_BACKGND_D);

  // get the byte size for each vertex. normally, the byte size is
  // ISP_BACKGND_T.skip + 3, but if parameter selection volume mode is in
  // effect and the shadow bit is 1, then the byte size is
  // ISP_BACKGND_T.skip * 2 + 3
  int vertex_size = pvr_.ISP_BACKGND_T.skip;
  if (!pvr_.FPU_SHAD_SCALE.intensity_volume_mode && pvr_.ISP_BACKGND_T.shadow) {
    vertex_size *= 2;
  }
  vertex_size = (vertex_size + 3) * 4;

  // skip to the first vertex
  vram_offset += pvr_.ISP_BACKGND_T.tag_offset * vertex_size;

  // copy vertex data to context
  for (int i = 0, bg_offset = 0; i < 3; i++) {
    CHECK_LE(bg_offset + vertex_size, (int)sizeof(tactx->bg_vertices));

    memory_.Memcpy(&tactx->bg_vertices[bg_offset], vram_offset, vertex_size);

    bg_offset += vertex_size;
    vram_offset += vertex_size;
  }
}
