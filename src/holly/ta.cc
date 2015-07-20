#include <float.h>
#include <stdint.h>
#include <algorithm>
#include <limits>
#include "holly/holly.h"
#include "holly/pvr2.h"
#include "holly/ta.h"

using namespace dreavm;
using namespace dreavm::core;
using namespace dreavm::emu;
using namespace dreavm::holly;
using namespace dreavm::renderer;

size_t GetParamSize_raw(const PCW &pcw, int vertex_type);
int GetPolyType_raw(const PCW &pcw);
int GetVertexType_raw(const PCW &pcw);

static Interrupt list_interrupts[] = {
    HOLLY_INTC_TAEOINT,   // TA_LIST_OPAQUE
    HOLLY_INTC_TAEOMINT,  // TA_LA_OPAQUE_MODVOL
    HOLLY_INTC_TAETINT,   // TA_LIST_TRANSLUCENT
    HOLLY_INTC_TAETMINT,  // TA_LIST_TRANSLUCENT_MODVOL
    HOLLY_INTC_TAEPTIN    // TA_LIST_PUNCH_THROUGH
};
static size_t param_size_lookup[0x100 * TA_NUM_PARAMS * TA_NUM_VERT_TYPES];
static int poly_type_lookup[0x100 * TA_NUM_PARAMS * TA_NUM_LISTS];
static int vertex_type_lookup[0x100 * TA_NUM_PARAMS * TA_NUM_LISTS];
static bool lookups_initialized;

void BuildLookupTables() {
  if (lookups_initialized) {
    return;
  }
  lookups_initialized = true;

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
size_t GetParamSize_raw(const PCW &pcw, int vertex_type) {
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
int GetVertexType_raw(const PCW &pcw) {
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

size_t GetParamSize(const PCW &pcw, int vertex_type) {
  size_t size =
      param_size_lookup[pcw.obj_control * TA_NUM_PARAMS * TA_NUM_VERT_TYPES +
                        pcw.para_type * TA_NUM_VERT_TYPES + vertex_type];
  CHECK_NE(size, 0);
  return size;
}

int GetPolyType(const PCW &pcw) {
  return poly_type_lookup[pcw.obj_control * TA_NUM_PARAMS * TA_NUM_LISTS +
                          pcw.para_type * TA_NUM_LISTS + pcw.list_type];
}

int GetVertexType(const PCW &pcw) {
  return vertex_type_lookup[pcw.obj_control * TA_NUM_PARAMS * TA_NUM_LISTS +
                            pcw.para_type * TA_NUM_LISTS + pcw.list_type];
}

inline DepthFunc TranslateDepthFunc(uint32_t depth_func) {
  static DepthFunc depth_funcs[] = {DEPTH_NEVER,  DEPTH_GREATER, DEPTH_EQUAL,
                                    DEPTH_GEQUAL, DEPTH_LESS,    DEPTH_NEQUAL,
                                    DEPTH_LEQUAL, DEPTH_ALWAYS};
  return depth_funcs[depth_func];
}

inline CullFace TranslateCull(uint32_t cull_mode) {
  static CullFace cull_modes[] = {CULL_NONE, CULL_NONE, CULL_FRONT, CULL_BACK};
  return cull_modes[cull_mode];
}

inline BlendFunc TranslateSrcBlendFunc(uint32_t blend_func) {
  static BlendFunc src_blend_funcs[] = {
      BLEND_ZERO, BLEND_ONE, BLEND_SRC_COLOR, BLEND_ONE_MINUS_SRC_COLOR,
      BLEND_SRC_ALPHA, BLEND_ONE_MINUS_SRC_ALPHA, BLEND_DST_ALPHA,
      BLEND_ONE_MINUS_DST_ALPHA};
  return src_blend_funcs[blend_func];
}

inline BlendFunc TranslateDstBlendFunc(uint32_t blend_func) {
  static BlendFunc dst_blend_funcs[] = {
      BLEND_ZERO, BLEND_ONE, BLEND_DST_COLOR, BLEND_ONE_MINUS_DST_COLOR,
      BLEND_SRC_ALPHA, BLEND_ONE_MINUS_SRC_ALPHA, BLEND_DST_ALPHA,
      BLEND_ONE_MINUS_DST_ALPHA};
  return dst_blend_funcs[blend_func];
}

inline ShadeMode TranslateShadeMode(uint32_t shade_mode) {
  static ShadeMode shade_modes[] = {SHADE_DECAL, SHADE_MODULATE,
                                    SHADE_DECAL_ALPHA, SHADE_MODULATE_ALPHA};
  return shade_modes[shade_mode];
}

TileAccelerator::TileAccelerator(Memory &memory, Holly &holly, PVR2 &pvr)
    : memory_(memory), holly_(holly), pvr_(pvr) {
  BuildLookupTables();
}

TileAccelerator::~TileAccelerator() {
  while (contexts_.size()) {
    const auto &it = contexts_.begin();

    TAContext *tactx = it->second;
    delete tactx;

    contexts_.erase(it);
  }
}

bool TileAccelerator::Init(Backend *rb) {
  memory_.Handle(0x10000000, 0x107fffff, 0x0, this, nullptr,
                 &TileAccelerator::WriteCmdInput);
  // TODO handle YUV transfers from 0x10800000 - 0x10ffffe0
  memory_.Handle(0x11000000, 0x11ffffff, 0x0, this, nullptr,
                 &TileAccelerator::WriteTexture);

  rb_ = rb;

  return true;
}

void TileAccelerator::SoftReset() {
  // FIXME what are we supposed to do here?
}

void TileAccelerator::InitContext(uint32_t addr) {
  TAContext *tactx = GetContext(addr);
  tactx->cursor = 0;
  tactx->size = 0;
  tactx->last_poly = nullptr;
  tactx->last_vertex = nullptr;
  tactx->list_type = 0;
  tactx->vertex_type = 0;
  tactx->num_surfs = 0;
  tactx->num_verts = 0;
  tactx->last_sorted_surf = 0;
}

void TileAccelerator::StartRender(uint32_t addr) {
  TAContext *tactx = GetContext(addr);

  uint8_t *data = tactx->data;
  uint8_t *end = tactx->data + tactx->size;

  ParseBackground(tactx);

  while (data < end) {
    PCW pcw = *(PCW *)data;

    // FIXME
    // If Vertex Parameters with the "End of Strip" specification were not
    // input, but parameters other than the Vertex Parameters were input, the
    // polygon data in question is ignored and an interrupt signal is output.

    switch (pcw.para_type) {
      // control params
      case TA_PARAM_END_OF_LIST:
        ParseEndOfList(tactx);
        break;

      case TA_PARAM_USER_TILE_CLIP:
        tactx->last_poly = nullptr;
        tactx->last_vertex = nullptr;
        break;

      case TA_PARAM_OBJ_LIST_SET:
        LOG(FATAL) << "TA_PARAM_OBJ_LIST_SET unsupported";
        break;

      // global params
      case TA_PARAM_POLY_OR_VOL:
        ParsePolyParam(tactx, reinterpret_cast<PolyParam *>(data));
        break;

      case TA_PARAM_SPRITE:
        ParsePolyParam(tactx, reinterpret_cast<PolyParam *>(data));
        break;

      // vertex params
      case TA_PARAM_VERTEX:
        ParseVertexParam(tactx, reinterpret_cast<VertexParam *>(data));
        break;

      default:
        debug_break();
        break;
    }

    data += GetParamSize(pcw, tactx->vertex_type);
  }

  NormalizeZ(tactx);

  LOG(INFO) << "StartRender " << tactx->num_surfs << " surfs, "
            << tactx->num_verts << " verts, " << tactx->size << " bytes";

  rb_->BindFramebuffer(FB_TILE_ACELLERATOR);
  rb_->Clear(0.1f, 0.39f, 0.88f, 1.0f);
  rb_->RenderSurfaces(tactx->surfs, tactx->num_surfs, tactx->verts,
                      tactx->num_verts, tactx->sorted_surfs);

  holly_.RequestInterrupt(HOLLY_INTC_PCEOVINT);
  holly_.RequestInterrupt(HOLLY_INTC_PCEOIINT);
  holly_.RequestInterrupt(HOLLY_INTC_PCEOTINT);
}

void TileAccelerator::WriteCmdInput(void *ctx, uint32_t addr, uint32_t value) {
  TileAccelerator *ta = (TileAccelerator *)ctx;
  TAContext *tactx = ta->GetContext(ta->pvr_.TA_ISP_BASE.base_address);

  CHECK_LT(tactx->size + 4, sizeof(tactx->data));
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
      ta->holly_.RequestInterrupt(list_interrupts[tactx->list_type]);
      tactx->last_poly = nullptr;
      tactx->last_vertex = nullptr;
      tactx->list_type = 0;
      tactx->vertex_type = 0;
    } else if (pcw.para_type == TA_PARAM_OBJ_LIST_SET) {
      LOG(FATAL) << "TA_PARAM_OBJ_LIST_SET unsupported";
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

void TileAccelerator::WriteTexture(void *ctx, uint32_t addr, uint32_t value) {
  TileAccelerator *ta = (TileAccelerator *)ctx;
  uint32_t dest_addr = TEXRAM_START + (addr & 0xeeffffff);

  // FIXME this is slow
  // FIXME this really shouldn't be here
  auto it = ta->pvr_.textures_.find(dest_addr);
  if (it != ta->pvr_.textures_.end()) {
    TextureHandle handle = it->second;
    ta->rb_->FreeTexture(handle);
    ta->pvr_.textures_.erase(it);
  }

  ta->memory_.W32(dest_addr, value);
}

bool TileAccelerator::AutoSortEnabled() {
  if (!pvr_.FPU_PARAM_CFG.region_header_type) {
    return !pvr_.ISP_FEED_CFG.presort;
  }
  uint32_t region_data = memory_.R32(VRAM_START + pvr_.REGION_BASE);
  return !(region_data & 0x20000000);
}

TAContext *TileAccelerator::GetContext(uint32_t addr) {
  auto it = contexts_.find(addr);

  if (it != contexts_.end()) {
    return it->second;
  }

  TAContext *ctx = new TAContext();
  ctx->addr = addr;

  auto result = contexts_.insert(std::make_pair(addr, ctx));
  return result.first->second;
}

Surface *TileAccelerator::AllocSurf(TAContext *tactx) {
  CHECK_LT(tactx->num_surfs, MAX_SURFACES);

  // reuse previous surface if it wasn't completed, else, allocate a new one
  int id;
  if (tactx->last_vertex && !tactx->last_vertex->type0.pcw.end_of_strip) {
    id = tactx->num_surfs - 1;
  } else {
    id = tactx->num_surfs++;
  }

  // reset the surface
  Surface *surf = &tactx->surfs[id];
  surf->first_vert = tactx->num_verts;
  surf->num_verts = 0;

  // default sort the surface
  tactx->sorted_surfs[id] = id;

  return surf;
}

Vertex *TileAccelerator::AllocVert(TAContext *tactx) {
  CHECK_LT(tactx->num_verts, MAX_VERTICES);
  Surface *surf = &tactx->surfs[tactx->num_surfs - 1];
  surf->num_verts++;
  return &tactx->verts[tactx->num_verts++];
}

// FIXME we could offload a lot of this to the GPU, generating shaders
// for different combinations of ISP/TSP parameters once the logic is
// ironed out
// FIXME honor use alpha
// FIXME honor ignore tex alpha
void TileAccelerator::ParseColor(TAContext *tactx, uint32_t base_color,
                                 float *color) {
  color[0] = ((base_color >> 16) & 0xff) / 255.0f;
  color[1] = ((base_color >> 8) & 0xff) / 255.0f;
  color[2] = (base_color & 0xff) / 255.0f;
  color[3] = ((base_color >> 24) & 0xff) / 255.0f;
}

void TileAccelerator::ParseColor(TAContext *tactx, float r, float g, float b,
                                 float a, float *color) {
  color[0] = r;
  color[1] = g;
  color[2] = b;
  color[3] = a;
}

void TileAccelerator::ParseColor(TAContext *tactx, float intensity,
                                 float *color) {
  color[0] = tactx->face_color[0] * intensity;
  color[1] = tactx->face_color[1] * intensity;
  color[2] = tactx->face_color[2] * intensity;
  color[3] = tactx->face_color[3];
}

void TileAccelerator::ParseOffsetColor(TAContext *tactx, uint32_t offset_color,
                                       float *color) {
  if (!tactx->last_poly->type0.isp_tsp.offset) {
    memset(color, 0, sizeof(float) * 4);
  } else {
    color[0] = ((offset_color >> 16) & 0xff) / 255.0f;
    color[1] = ((offset_color >> 8) & 0xff) / 255.0f;
    color[2] = (offset_color & 0xff) / 255.0f;
    color[3] = ((offset_color >> 24) & 0xff) / 255.0f;
  }
}

void TileAccelerator::ParseOffsetColor(TAContext *tactx, float r, float g,
                                       float b, float a, float *color) {
  if (!tactx->last_poly->type0.isp_tsp.offset) {
    memset(color, 0, sizeof(float) * 4);
  } else {
    color[0] = r;
    color[1] = g;
    color[2] = b;
    color[3] = a;
  }
}

void TileAccelerator::ParseOffsetColor(TAContext *tactx, float intensity,
                                       float *color) {
  if (!tactx->last_poly->type0.isp_tsp.offset) {
    memset(color, 0, sizeof(float) * 4);
  } else {
    color[0] = tactx->face_offset_color[0] * intensity;
    color[1] = tactx->face_offset_color[1] * intensity;
    color[2] = tactx->face_offset_color[2] * intensity;
    color[3] = tactx->face_offset_color[3];
  }
}

void TileAccelerator::ParseBackground(TAContext *tactx) {
  auto ParseBackgroundVertex =
      [&](const ISP_TSP &isp, uint32_t vertex_addr, Vertex *v) {
        v->xyz[0] = memory_.RF32(vertex_addr);
        v->xyz[1] = memory_.RF32(vertex_addr + 4);
        v->xyz[2] = *(float *)&pvr_.ISP_BACKGND_D;
        vertex_addr += 12;

        if (isp.texture) {
          v->uv[0] = memory_.RF32(vertex_addr);
          v->uv[1] = memory_.RF32(vertex_addr + 4);
          vertex_addr += 8;
          debug_break();
        }

        uint32_t base_color = memory_.R32(vertex_addr);
        v->color[0] = ((base_color >> 16) & 0xff) / 255.0f;
        v->color[1] = ((base_color >> 8) & 0xff) / 255.0f;
        v->color[2] = (base_color & 0xff) / 255.0f;
        v->color[3] = ((base_color >> 24) & 0xff) / 255.0f;
        vertex_addr += 4;

        if (isp.offset) {
          uint32_t offset_color = memory_.R32(vertex_addr);
          v->offset_color[0] = ((offset_color >> 16) & 0xff) / 255.0f;
          v->offset_color[1] = ((offset_color >> 16) & 0xff) / 255.0f;
          v->offset_color[2] = ((offset_color >> 16) & 0xff) / 255.0f;
          v->offset_color[3] = 0.0f;
          vertex_addr += 4;
          debug_break();
        }
      };

  // according to the hardware docs, this is the correct calculation of the
  // background ISP address. however, in practice, the second TA buffer's ISP
  // address comes out to be 0x800000 when booting the the bios when the vram
  // is only 8mb total. by examining a raw memory dump, the ISP data is only
  // ever available at 0x0 when booting the bios, so masking this seems to
  // be the correct solution
  uint32_t vram_offset =
      VRAM_START +
      ((tactx->addr + pvr_.ISP_BACKGND_T.tag_address * 4) & 0x7fffff);

  // get surface parameters
  ISP_TSP isp;
  // TSP tsp;
  // TCW tcw;
  isp.full = memory_.R32(vram_offset);
  // tsp.full = memory_.R32(vram_offset + 4);
  // tcw.full = memory_.R32(vram_offset + 8);
  vram_offset += 12;

  // get the byte size for each vertex. normally, the byte size is
  // "ISP_BACKGND_T.skip + 3," but if parameter selection volume mode is in
  // effect and the shadow bit is "1," then the byte size is
  // "ISP_BACKGND_Tskip * 2 + 3"
  int vertex_size = pvr_.ISP_BACKGND_T.skip;
  if (!pvr_.FPU_SHAD_SCALE.intensity_volume_mode && pvr_.ISP_BACKGND_T.shadow) {
    vertex_size *= 2;
  }
  vertex_size = (vertex_size + 3) * 4;

  // translate the surface
  Surface *surf = AllocSurf(tactx);
  surf->texture = 0;
  surf->depth_write = !isp.z_write_disable;
  surf->depth_func = TranslateDepthFunc(isp.depth_compare_mode);
  surf->cull = TranslateCull(isp.culling_mode);
  surf->src_blend = BLEND_NONE;
  surf->dst_blend = BLEND_NONE;

  // skip to the first vertex
  vram_offset += pvr_.ISP_BACKGND_T.tag_offset * vertex_size;

  // translate the first 3 vertices
  Vertex *v[4] = {nullptr};
  for (int i = 0; i < 3; i++) {
    v[i] = AllocVert(tactx);
    ParseBackgroundVertex(isp, vram_offset, v[i]);
    vram_offset += vertex_size;
  }

  // 4th vertex isn't supplied, fill it out automatically
  v[3] = AllocVert(tactx);
  v[3]->xyz[0] = v[1]->xyz[0];
  v[3]->xyz[1] = v[2]->xyz[1];
  v[3]->xyz[2] = v[0]->xyz[2];
  v[3]->color[0] = v[0]->color[0];
  v[3]->color[1] = v[0]->color[1];
  v[3]->color[2] = v[0]->color[2];
  v[3]->color[3] = v[0]->color[3];
  v[3]->uv[0] = v[1]->uv[0];
  v[3]->uv[1] = v[2]->uv[1];
}

// NOTE this offset color implementation is not correct at all
//      see the Texture/Shading Instruction in the TSP instruction word
// ALSO check out 16bit uv flag
void TileAccelerator::ParsePolyParam(TAContext *tactx, const PolyParam *param) {
  tactx->last_poly = param;
  tactx->last_vertex = nullptr;
  tactx->list_type = param->type0.pcw.list_type;
  tactx->vertex_type = GetVertexType(param->type0.pcw);
  tactx->face_color[0] = tactx->face_color[1] = tactx->face_color[2] =
      tactx->face_color[3] = 0.0f;
  tactx->face_offset_color[0] = tactx->face_offset_color[1] =
      tactx->face_offset_color[2] = tactx->face_offset_color[3] = 0.0f;

  // setup the new surface
  Surface *surf = AllocSurf(tactx);
  surf->depth_write = !param->type0.isp_tsp.z_write_disable;
  surf->depth_func =
      TranslateDepthFunc(param->type0.isp_tsp.depth_compare_mode);
  surf->cull = TranslateCull(param->type0.isp_tsp.culling_mode);
  surf->src_blend = TranslateSrcBlendFunc(param->type0.tsp.src_alpha_instr);
  surf->dst_blend = TranslateDstBlendFunc(param->type0.tsp.dst_alpha_instr);
  surf->shade = TranslateShadeMode(param->type0.tsp.texture_shading_instr);

  // override a few surface parameters based on the list type
  if (tactx->list_type != TA_LIST_TRANSLUCENT &&
      tactx->list_type != TA_LIST_TRANSLUCENT_MODVOL) {
    surf->src_blend = BLEND_NONE;
    surf->dst_blend = BLEND_NONE;
  } else if (tactx->list_type == TA_LIST_TRANSLUCENT &&
             tactx->list_type == TA_LIST_TRANSLUCENT_MODVOL &&
             AutoSortEnabled()) {
    surf->depth_func = DEPTH_LEQUAL;
  } else if (tactx->list_type == TA_LIST_PUNCH_THROUGH) {
    surf->depth_func = DEPTH_GEQUAL;
  }

  surf->texture = param->type0.pcw.texture
                      ? pvr_.GetTexture(param->type0.tsp, param->type0.tcw)
                      : 0;

  int poly_type = GetPolyType(param->type0.pcw);
  switch (poly_type) {
    case 0: {
      // uint32_t sdma_data_size;
      // uint32_t sdma_next_addr;
    } break;

    case 1: {
      tactx->face_color[0] = param->type1.face_color_r;
      tactx->face_color[1] = param->type1.face_color_g;
      tactx->face_color[2] = param->type1.face_color_b;
      tactx->face_color[3] = param->type1.face_color_a;
    } break;

    case 2: {
      tactx->face_color[0] = param->type2.face_color_r;
      tactx->face_color[1] = param->type2.face_color_g;
      tactx->face_color[2] = param->type2.face_color_b;
      tactx->face_color[3] = param->type2.face_color_a;
      tactx->face_offset_color[0] = param->type2.face_offset_color_r;
      tactx->face_offset_color[1] = param->type2.face_offset_color_g;
      tactx->face_offset_color[2] = param->type2.face_offset_color_b;
      tactx->face_offset_color[3] = param->type2.face_offset_color_a;
    } break;

    case 5: {
      tactx->face_color[0] = ((param->sprite.base_color >> 16) & 0xff) / 255.0f;
      tactx->face_color[1] = ((param->sprite.base_color >> 8) & 0xff) / 255.0f;
      tactx->face_color[2] = (param->sprite.base_color & 0xff) / 255.0f;
      tactx->face_color[3] = ((param->sprite.base_color >> 24) & 0xff) / 255.0f;
      tactx->face_offset_color[0] =
          ((param->sprite.offset_color >> 16) & 0xff) / 255.0f;
      tactx->face_offset_color[1] =
          ((param->sprite.offset_color >> 8) & 0xff) / 255.0f;
      tactx->face_offset_color[2] =
          (param->sprite.offset_color & 0xff) / 255.0f;
      tactx->face_offset_color[3] =
          ((param->sprite.offset_color >> 24) & 0xff) / 255.0f;
    } break;

    case 6: {
      // don't do anything with modifier volume yet
      tactx->num_surfs--;
    } break;

    default:
      debug_break();
      break;
  }
}

void TileAccelerator::ParseVertexParam(TAContext *tactx,
                                       const VertexParam *param) {
  // If there is no need to change the Global Parameters, a Vertex Parameter for
  // the next polygon may be input immediately after inputting a Vertex
  // Parameter for which "End of Strip" was specified.
  if (tactx->last_vertex && tactx->last_vertex->type0.pcw.end_of_strip) {
    // start a new surface
    ParsePolyParam(tactx, tactx->last_poly);
  }
  tactx->last_vertex = param;

  switch (tactx->vertex_type) {
    case 0: {
      Vertex *vert = AllocVert(tactx);
      vert->xyz[0] = param->type0.xyz[0];
      vert->xyz[1] = param->type0.xyz[1];
      vert->xyz[2] = param->type0.xyz[2];
      ParseColor(tactx, param->type0.base_color, vert->color);
      vert->offset_color[1] = 0.0f;
      vert->offset_color[2] = 0.0f;
      vert->offset_color[3] = 0.0f;
      vert->uv[0] = 0.0f;
      vert->uv[1] = 0.0f;
    } break;

    case 1: {
      Vertex *vert = AllocVert(tactx);
      vert->xyz[0] = param->type1.xyz[0];
      vert->xyz[1] = param->type1.xyz[1];
      vert->xyz[2] = param->type1.xyz[2];
      ParseColor(tactx, param->type1.base_color_r, param->type1.base_color_g,
                 param->type1.base_color_b, param->type1.base_color_a,
                 vert->color);
      vert->offset_color[0] = 0.0f;
      vert->offset_color[1] = 0.0f;
      vert->offset_color[2] = 0.0f;
      vert->offset_color[3] = 0.0f;
      vert->uv[0] = 0.0f;
      vert->uv[1] = 0.0f;
    } break;

    case 2: {
      Vertex *vert = AllocVert(tactx);
      vert->xyz[0] = param->type2.xyz[0];
      vert->xyz[1] = param->type2.xyz[1];
      vert->xyz[2] = param->type2.xyz[2];
      ParseColor(tactx, param->type2.base_intensity, vert->color);
      vert->offset_color[0] = 0.0f;
      vert->offset_color[1] = 0.0f;
      vert->offset_color[2] = 0.0f;
      vert->offset_color[3] = 0.0f;
      vert->uv[0] = 0.0f;
      vert->uv[1] = 0.0f;
    } break;

    case 3: {
      Vertex *vert = AllocVert(tactx);
      vert->xyz[0] = param->type3.xyz[0];
      vert->xyz[1] = param->type3.xyz[1];
      vert->xyz[2] = param->type3.xyz[2];
      ParseColor(tactx, param->type3.base_color, vert->color);
      ParseOffsetColor(tactx, param->type3.offset_color, vert->offset_color);
      vert->uv[0] = param->type3.uv[0];
      vert->uv[1] = param->type3.uv[1];
    } break;

    case 4: {
      Vertex *vert = AllocVert(tactx);
      vert->xyz[0] = param->type4.xyz[0];
      vert->xyz[1] = param->type4.xyz[1];
      vert->xyz[2] = param->type4.xyz[2];
      ParseColor(tactx, param->type4.base_color, vert->color);
      ParseOffsetColor(tactx, param->type4.offset_color, vert->offset_color);
      uint32_t u = param->type4.uv[0] << 16;
      uint32_t v = param->type4.uv[0] << 16;
      vert->uv[0] = *reinterpret_cast<float *>(&u);
      vert->uv[1] = *reinterpret_cast<float *>(&v);
    } break;

    case 5: {
      Vertex *vert = AllocVert(tactx);
      vert->xyz[0] = param->type5.xyz[0];
      vert->xyz[1] = param->type5.xyz[1];
      vert->xyz[2] = param->type5.xyz[2];
      ParseColor(tactx, param->type5.base_color_r, param->type5.base_color_g,
                 param->type5.base_color_b, param->type5.base_color_a,
                 vert->color);
      ParseOffsetColor(tactx, param->type5.offset_color_r,
                       param->type5.offset_color_g, param->type5.offset_color_b,
                       param->type5.offset_color_a, vert->offset_color);
      vert->uv[0] = param->type5.uv[0];
      vert->uv[1] = param->type5.uv[1];
    } break;

    case 6: {
      Vertex *vert = AllocVert(tactx);
      vert->xyz[0] = param->type6.xyz[0];
      vert->xyz[1] = param->type6.xyz[1];
      vert->xyz[2] = param->type6.xyz[2];
      ParseColor(tactx, param->type6.base_color_r, param->type6.base_color_g,
                 param->type6.base_color_b, param->type6.base_color_a,
                 vert->color);
      ParseOffsetColor(tactx, param->type6.offset_color_r,
                       param->type6.offset_color_g, param->type6.offset_color_b,
                       param->type6.offset_color_a, vert->offset_color);
      uint32_t u = param->type6.uv[0] << 16;
      uint32_t v = param->type6.uv[0] << 16;
      vert->uv[0] = *reinterpret_cast<float *>(&u);
      vert->uv[1] = *reinterpret_cast<float *>(&v);
    } break;

    case 7: {
      Vertex *vert = AllocVert(tactx);
      vert->xyz[0] = param->type7.xyz[0];
      vert->xyz[1] = param->type7.xyz[1];
      vert->xyz[2] = param->type7.xyz[2];
      ParseColor(tactx, param->type7.base_intensity, vert->color);
      ParseOffsetColor(tactx, param->type7.offset_intensity,
                       vert->offset_color);
      vert->uv[0] = param->type7.uv[0];
      vert->uv[1] = param->type7.uv[1];
    } break;

    case 8: {
      Vertex *vert = AllocVert(tactx);
      vert->xyz[0] = param->type8.xyz[0];
      vert->xyz[1] = param->type8.xyz[1];
      vert->xyz[2] = param->type8.xyz[2];
      ParseColor(tactx, param->type8.base_intensity, vert->color);
      ParseOffsetColor(tactx, param->type8.offset_intensity,
                       vert->offset_color);
      uint32_t u = param->type8.uv[0] << 16;
      uint32_t v = param->type8.uv[0] << 16;
      vert->uv[0] = *reinterpret_cast<float *>(&u);
      vert->uv[1] = *reinterpret_cast<float *>(&v);
    } break;

    case 15:
      debug_break();
      break;

    case 16: {
      CHECK_EQ(param->sprite1.pcw.end_of_strip, 1);

      auto ParseSpriteVert = [&](int i, Vertex *vert) {
        vert->xyz[0] = param->sprite1.xyz[i][0];
        vert->xyz[1] = param->sprite1.xyz[i][1];
        // FIXME this is assuming all sprites are billboards
        // z isn't specified for i == 3
        vert->xyz[2] = param->sprite1.xyz[0][2];
        ParseColor(tactx, 1.0f, 1.0f, 1.0f, 1.0f, vert->color);
        ParseOffsetColor(tactx, 1.0f, 1.0f, 1.0f, 1.0f, vert->offset_color);
        uint32_t u, v;
        if (i == 3) {
          u = (param->sprite1.uv[0] & 0xffff0000);
          v = (param->sprite1.uv[2] & 0x0000ffff) << 16;
        } else {
          u = (param->sprite1.uv[i] & 0xffff0000);
          v = (param->sprite1.uv[i] & 0x0000ffff) << 16;
        }
        vert->uv[0] = *reinterpret_cast<float *>(&u);
        vert->uv[1] = *reinterpret_cast<float *>(&v);
      };

      ParseSpriteVert(0, AllocVert(tactx));
      ParseSpriteVert(1, AllocVert(tactx));
      ParseSpriteVert(3, AllocVert(tactx));
      ParseSpriteVert(2, AllocVert(tactx));
    } break;

    case 17: {
    } break;

    default:
      LOG(FATAL) << "Unsupported vertex type " << tactx->vertex_type;
      break;
  }

  // In the case of the Polygon type, the last Vertex Parameter for an object
  // must have "End of Strip" specified.  If Vertex Parameters with the "End of
  // Strip" specification were not input, but parameters other than the Vertex
  // Parameters were input, the polygon data in question is ignored and an
  // interrupt signal is output.
  // FIXME is this true for sprites which come through this path as well?
}

void TileAccelerator::ParseEndOfList(TAContext *tactx) {
  int first_surf = tactx->last_sorted_surf;
  int num_surfs = tactx->num_surfs - tactx->last_sorted_surf;

  // sort transparent polys by their z value, from back to front. remember, in
  // dreamcast coordinates smaller z values are further away from the camera
  if ((tactx->list_type == TA_LIST_TRANSLUCENT ||
       tactx->list_type == TA_LIST_TRANSLUCENT_MODVOL) &&
      AutoSortEnabled()) {
    int *first = tactx->sorted_surfs + first_surf;
    int *last = tactx->sorted_surfs + first_surf + num_surfs;
    std::sort(first, last, [&](int a, int b) {
      Surface *surfa = &tactx->surfs[a];
      Surface *surfb = &tactx->surfs[b];

      float minza = std::numeric_limits<float>::max();
      for (int i = 0; i < surfa->num_verts; i++) {
        Vertex *v = &tactx->verts[surfa->first_vert + i];
        if (v->xyz[2] < minza) {
          minza = v->xyz[2];
        }
      }
      float minzb = std::numeric_limits<float>::max();
      for (int i = 0; i < surfb->num_verts; i++) {
        Vertex *v = &tactx->verts[surfb->first_vert + i];
        if (v->xyz[2] < minzb) {
          minzb = v->xyz[2];
        }
      }

      return minza < minzb;
    });
  }

  tactx->last_poly = nullptr;
  tactx->last_vertex = nullptr;
  tactx->last_sorted_surf = tactx->num_surfs;
}

// Vertices coming into the TA are already in window space. However, the Z
// component of these vertices isn't normalized to a particular range, but
// they do sort correctly. In order to render the vertices correctly, the
// Z components need to be normalized between 0.0 and 1.0.
void TileAccelerator::NormalizeZ(TAContext *tactx) {
  float znear = std::numeric_limits<float>::min();
  float zfar = std::numeric_limits<float>::max();

  // get the bounds of the Z components
  for (int i = 0; i < tactx->num_verts; i++) {
    Vertex *v = &tactx->verts[i];
    if (v->xyz[2] > znear) {
      znear = v->xyz[2];
    }
    if (v->xyz[2] < zfar) {
      zfar = v->xyz[2];
    }
  }

  // normalize each Z component between 0.0 and 1.0, fudging a bit to deal
  // with depth buffer precision
  float zdelta = znear - zfar;
  if (zdelta <= 0.0f) {
    zdelta = 1.0f;
  }
  for (int i = 0; i < tactx->num_verts; i++) {
    Vertex *v = &tactx->verts[i];
    v->xyz[2] = 0.99f - ((v->xyz[2] - zfar) / zdelta) * 0.99f;
  }
}
