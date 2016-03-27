#include "core/assert.h"
#include "emu/profiler.h"
#include "hw/holly/pixel_convert.h"
#include "hw/holly/tile_accelerator.h"
#include "hw/holly/tile_renderer.h"

using namespace re::hw::holly;
using namespace re::renderer;

static int compressed_mipmap_offsets[] = {
    0x00006,  // 8 x 8
    0x00016,  // 16 x 16
    0x00056,  // 32 x 32
    0x00156,  // 64 x 64
    0x00556,  // 128 x 128
    0x01556,  // 256 x 256
    0x05556,  // 512 x 512
    0x15556,  // 1024 x 1024
};

static int paletted_4bpp_mipmap_offsets[] = {
    0x0000c,  // 8 x 8
    0x0002c,  // 16 x 16
    0x000ac,  // 32 x 32
    0x002ac,  // 64 x 64
    0x00aac,  // 128 x 128
    0x02aac,  // 256 x 256
    0x0aaac,  // 512 x 512
    0x2aaac,  // 1024 x 1024
};

static int paletted_8bpp_mipmap_offsets[] = {
    0x00018,  // 8 x 8
    0x00058,  // 16 x 16
    0x00158,  // 32 x 32
    0x00558,  // 64 x 64
    0x01558,  // 128 x 128
    0x05558,  // 256 x 256
    0x15558,  // 512 x 512
    0x55558,  // 1024 x 1024
};

static int nonpaletted_mipmap_offsets[] = {
    0x00030,  // 8 x 8
    0x000b0,  // 16 x 16
    0x002b0,  // 32 x 32
    0x00ab0,  // 64 x 64
    0x02ab0,  // 128 x 128
    0x0aab0,  // 256 x 256
    0x2aab0,  // 512 x 512
    0xaaab0,  // 1024 x 1024
};

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
      BLEND_ZERO,      BLEND_ONE,
      BLEND_DST_COLOR, BLEND_ONE_MINUS_DST_COLOR,
      BLEND_SRC_ALPHA, BLEND_ONE_MINUS_SRC_ALPHA,
      BLEND_DST_ALPHA, BLEND_ONE_MINUS_DST_ALPHA};
  return src_blend_funcs[blend_func];
}

inline BlendFunc TranslateDstBlendFunc(uint32_t blend_func) {
  static BlendFunc dst_blend_funcs[] = {
      BLEND_ZERO,      BLEND_ONE,
      BLEND_SRC_COLOR, BLEND_ONE_MINUS_SRC_COLOR,
      BLEND_SRC_ALPHA, BLEND_ONE_MINUS_SRC_ALPHA,
      BLEND_DST_ALPHA, BLEND_ONE_MINUS_DST_ALPHA};
  return dst_blend_funcs[blend_func];
}

inline ShadeMode TranslateShadeMode(uint32_t shade_mode) {
  static ShadeMode shade_modes[] = {SHADE_DECAL, SHADE_MODULATE,
                                    SHADE_DECAL_ALPHA, SHADE_MODULATE_ALPHA};
  return shade_modes[shade_mode];
}

static inline uint32_t abgr_to_rgba(uint32_t v) {
  return (v & 0xff000000) | ((v & 0xff) << 16) | (v & 0xff00) |
         ((v & 0xff0000) >> 16);
}

static inline uint8_t float_to_u8(float x) {
  return std::min(std::max((uint32_t)(x * 255.0f), 0u), 255u);
}

static inline uint32_t float_to_rgba(float r, float g, float b, float a) {
  return (float_to_u8(a) << 24) | (float_to_u8(b) << 16) |
         (float_to_u8(g) << 8) | float_to_u8(r);
}

TextureKey TextureProvider::GetTextureKey(const TSP &tsp, const TCW &tcw) {
  return ((uint64_t)tsp.full << 32) | tcw.full;
}

TileRenderer::TileRenderer(Backend &rb, TextureProvider &texture_provider)
    : rb_(rb), texture_provider_(texture_provider) {}

void TileRenderer::ParseContext(const TileContext &tctx,
                                TileRenderContext *rctx, bool map_params) {
  PROFILER_GPU("TileRenderer::ParseContext");

  const uint8_t *data = tctx.data;
  const uint8_t *end = tctx.data + tctx.size;

  Reset(rctx);

  ParseBackground(tctx, rctx);

  while (data < end) {
    PCW pcw = *(PCW *)data;

    // FIXME
    // If Vertex Parameters with the "End of Strip" specification were not
    // input, but parameters other than the Vertex Parameters were input, the
    // polygon data in question is ignored and an interrupt signal is output.

    switch (pcw.para_type) {
      // control params
      case TA_PARAM_END_OF_LIST:
        ParseEndOfList(tctx, rctx, data);
        break;

      case TA_PARAM_USER_TILE_CLIP:
        // nothing to do
        break;

      case TA_PARAM_OBJ_LIST_SET:
        LOG_FATAL("TA_PARAM_OBJ_LIST_SET unsupported");
        break;

      // global params
      case TA_PARAM_POLY_OR_VOL:
        ParsePolyParam(tctx, rctx, data);
        break;

      case TA_PARAM_SPRITE:
        ParsePolyParam(tctx, rctx, data);
        break;

      // vertex params
      case TA_PARAM_VERTEX:
        ParseVertexParam(tctx, rctx, data);
        break;

      default:
        LOG_FATAL("Unsupported parameter type %d", pcw.para_type);
        break;
    }

    // map ta parameters to their translated surfaces / vertices
    if (map_params) {
      int offset = static_cast<int>(data - tctx.data);
      rctx->param_map[offset] = {rctx->surfs.size(), rctx->verts.size()};
    }

    data += TileAccelerator::GetParamSize(pcw, vertex_type_);
  }

  FillProjectionMatrix(tctx, rctx);
}

void TileRenderer::RenderContext(const TileRenderContext &rctx) {
  auto &surfs = rctx_.surfs;
  auto &verts = rctx_.verts;
  auto &sorted_surfs = rctx_.sorted_surfs;

  rb_.BeginSurfaces(rctx_.projection, verts.data(), verts.size());

  for (int i = 0, n = surfs.size(); i < n; i++) {
    rb_.DrawSurface(surfs[sorted_surfs[i]]);
  }

  rb_.EndSurfaces();
}

void TileRenderer::RenderContext(const TileContext &tctx) {
  ParseContext(tctx, &rctx_, false);
  RenderContext(rctx_);
}

void TileRenderer::Reset(TileRenderContext *rctx) {
  // reset render state
  rctx->surfs.Clear();
  rctx->verts.Clear();
  rctx->sorted_surfs.Clear();
  rctx->param_map.clear();

  // reset global state
  last_poly_ = nullptr;
  last_vertex_ = nullptr;
  list_type_ = 0;
  vertex_type_ = 0;
  last_sorted_surf_ = 0;
}

Surface &TileRenderer::AllocSurf(TileRenderContext *rctx, bool copy_from_prev) {
  auto &surfs = rctx->surfs;

  int id = surfs.size();
  surfs.Resize(id + 1);
  Surface &surf = surfs[id];

  // either reset the surface state, or copy the state from the previous surface
  if (copy_from_prev) {
    new (&surf) Surface(surfs[id - 1]);
  } else {
    new (&surf) Surface();
  }

  // star verts at the end
  surf.first_vert = rctx->verts.size();
  surf.num_verts = 0;

  // default sort the surface
  rctx->sorted_surfs.Resize(id + 1);
  rctx->sorted_surfs[id] = id;

  return surf;
}

Vertex &TileRenderer::AllocVert(TileRenderContext *rctx) {
  auto &surfs = rctx->surfs;
  auto &verts = rctx->verts;

  int id = verts.size();
  verts.Resize(id + 1);
  Vertex &v = verts[id];

  new (&v) Vertex();

  // update vertex count on the current surface
  Surface &surf = surfs.back();
  surf.num_verts++;

  return v;
}

void TileRenderer::DiscardIncompleteSurf(TileRenderContext *rctx) {
  // free up the last surface if it wasn't finished
  if (last_vertex_ && !last_vertex_->type0.pcw.end_of_strip) {
    rctx->surfs.PopBack();
  }
}

// FIXME we could offload a lot of this to the GPU, generating shaders
// for different combinations of ISP/TSP parameters once the logic is
// ironed out
// FIXME honor use alpha
// FIXME honor ignore tex alpha
void TileRenderer::ParseColor(uint32_t base_color, uint32_t *color) {
  *color = abgr_to_rgba(base_color);

  // if (!last_poly_->type0.tsp.use_alpha) {
  //   color[3] = 1.0f;
  // }
}

void TileRenderer::ParseColor(float r, float g, float b, float a,
                              uint32_t *color) {
  *color = float_to_rgba(r, g, b, a);

  // if (!last_poly_->type0.tsp.use_alpha) {
  //   color[3] = 1.0f;
  // }
}

void TileRenderer::ParseColor(float intensity, uint32_t *color) {
  *color = float_to_rgba(face_color_[0] * intensity, face_color_[1] * intensity,
                         face_color_[2] * intensity, face_color_[3]);

  // if (!last_poly_->type0.tsp.use_alpha) {
  //   color[3] = 1.0f;
  // }
}

void TileRenderer::ParseOffsetColor(uint32_t offset_color, uint32_t *color) {
  if (!last_poly_->type0.isp_tsp.offset) {
    *color = 0;
  } else {
    *color = abgr_to_rgba(offset_color);
  }
}

void TileRenderer::ParseOffsetColor(float r, float g, float b, float a,
                                    uint32_t *color) {
  if (!last_poly_->type0.isp_tsp.offset) {
    *color = 0;
  } else {
    *color = float_to_rgba(r, g, b, a);
  }
}

void TileRenderer::ParseOffsetColor(float intensity, uint32_t *color) {
  if (!last_poly_->type0.isp_tsp.offset) {
    *color = 0;
  } else {
    *color = float_to_rgba(
        face_offset_color_[0] * intensity, face_offset_color_[1] * intensity,
        face_offset_color_[2] * intensity, face_offset_color_[3]);
  }
}

void TileRenderer::ParseBackground(const TileContext &tctx,
                                   TileRenderContext *rctx) {
  // translate the surface
  Surface &surf = AllocSurf(rctx, false);

  surf.texture = 0;
  surf.depth_write = !tctx.bg_isp.z_write_disable;
  surf.depth_func = TranslateDepthFunc(tctx.bg_isp.depth_compare_mode);
  surf.cull = TranslateCull(tctx.bg_isp.culling_mode);
  surf.src_blend = BLEND_NONE;
  surf.dst_blend = BLEND_NONE;

  // translate the first 3 vertices
  Vertex &v0 = AllocVert(rctx);
  Vertex &v1 = AllocVert(rctx);
  Vertex &v2 = AllocVert(rctx);
  Vertex &v3 = AllocVert(rctx);

  int offset = 0;
  auto ParseBackgroundVert = [&](int i, Vertex &v) {
    v.xyz[0] = re::load<float>(&tctx.bg_vertices[offset]);
    v.xyz[1] = re::load<float>(&tctx.bg_vertices[offset + 4]);
    v.xyz[2] = re::load<float>(&tctx.bg_vertices[offset + 8]);
    offset += 12;

    if (tctx.bg_isp.texture) {
      LOG_FATAL("Unsupported bg_isp.texture");
      // v.uv[0] = re::load<float>(&tctx.bg_vertices[offset]);
      // v.uv[1] = re::load<float>(&tctx.bg_vertices[offset + 4]);
      // offset += 8;
    }

    uint32_t base_color = re::load<uint32_t>(&tctx.bg_vertices[offset]);
    v.color = abgr_to_rgba(base_color);
    offset += 4;

    if (tctx.bg_isp.offset) {
      LOG_FATAL("Unsupported bg_isp.offset");
      // uint32_t offset_color =
      // re::load<uint32_t>(&tctx.bg_vertices[offset]);
      // v.offset_color[0] = ((offset_color >> 16) & 0xff) / 255.0f;
      // v.offset_color[1] = ((offset_color >> 16) & 0xff) / 255.0f;
      // v.offset_color[2] = ((offset_color >> 16) & 0xff) / 255.0f;
      // v.offset_color[3] = 0.0f;
      // offset += 4;
    }
  };

  ParseBackgroundVert(0, v0);
  ParseBackgroundVert(1, v1);
  ParseBackgroundVert(2, v2);

  // override the xyz values supplied by ISP_BACKGND_T. while the hardware docs
  // act like the should be correct, they're most definitely not in most cases
  v0.xyz[0] = 0.0f;
  v0.xyz[1] = (float)tctx.video_height;
  v0.xyz[2] = tctx.bg_depth;
  v1.xyz[0] = 0.0f;
  v1.xyz[1] = 0.0f;
  v1.xyz[2] = tctx.bg_depth;
  v2.xyz[0] = (float)tctx.video_width;
  v2.xyz[1] = (float)tctx.video_height;
  v2.xyz[2] = tctx.bg_depth;

  // 4th vertex isn't supplied, fill it out automatically
  v3.xyz[0] = v2.xyz[0];
  v3.xyz[1] = v1.xyz[1];
  v3.xyz[2] = tctx.bg_depth;
  v3.color = v0.color;
  v3.offset_color = v0.offset_color;
  v3.uv[0] = v2.uv[0];
  v3.uv[1] = v1.uv[1];
}

// NOTE this offset color implementation is not correct at all, see the
// Texture/Shading Instruction in the TSP instruction word
void TileRenderer::ParsePolyParam(const TileContext &tctx,
                                  TileRenderContext *rctx,
                                  const uint8_t *data) {
  DiscardIncompleteSurf(rctx);

  const PolyParam *param = reinterpret_cast<const PolyParam *>(data);

  last_poly_ = param;
  last_vertex_ = nullptr;
  list_type_ = param->type0.pcw.list_type;
  vertex_type_ = TileAccelerator::GetVertexType(param->type0.pcw);

  int poly_type = TileAccelerator::GetPolyType(param->type0.pcw);
  switch (poly_type) {
    case 0: {
      // uint32_t sdma_data_size;
      // uint32_t sdma_next_addr;
    } break;

    case 1: {
      face_color_[0] = param->type1.face_color_r;
      face_color_[1] = param->type1.face_color_g;
      face_color_[2] = param->type1.face_color_b;
      face_color_[3] = param->type1.face_color_a;
    } break;

    case 2: {
      face_color_[0] = param->type2.face_color_r;
      face_color_[1] = param->type2.face_color_g;
      face_color_[2] = param->type2.face_color_b;
      face_color_[3] = param->type2.face_color_a;
      face_offset_color_[0] = param->type2.face_offset_color_r;
      face_offset_color_[1] = param->type2.face_offset_color_g;
      face_offset_color_[2] = param->type2.face_offset_color_b;
      face_offset_color_[3] = param->type2.face_offset_color_a;
    } break;

    case 5: {
      face_color_[0] = ((param->sprite.base_color >> 16) & 0xff) / 255.0f;
      face_color_[1] = ((param->sprite.base_color >> 8) & 0xff) / 255.0f;
      face_color_[2] = (param->sprite.base_color & 0xff) / 255.0f;
      face_color_[3] = ((param->sprite.base_color >> 24) & 0xff) / 255.0f;
      face_offset_color_[0] =
          ((param->sprite.offset_color >> 16) & 0xff) / 255.0f;
      face_offset_color_[1] =
          ((param->sprite.offset_color >> 8) & 0xff) / 255.0f;
      face_offset_color_[2] = (param->sprite.offset_color & 0xff) / 255.0f;
      face_offset_color_[3] =
          ((param->sprite.offset_color >> 24) & 0xff) / 255.0f;
    } break;

    case 6:
      // don't do anything with modifier volume yet
      return;

    default:
      LOG_FATAL("Unsupported poly type %d", poly_type);
      break;
  }

  // setup the new surface
  Surface &surf = AllocSurf(rctx, false);
  surf.depth_write = !param->type0.isp_tsp.z_write_disable;
  surf.depth_func = TranslateDepthFunc(param->type0.isp_tsp.depth_compare_mode);
  surf.cull = TranslateCull(param->type0.isp_tsp.culling_mode);
  surf.src_blend = TranslateSrcBlendFunc(param->type0.tsp.src_alpha_instr);
  surf.dst_blend = TranslateDstBlendFunc(param->type0.tsp.dst_alpha_instr);
  surf.shade = TranslateShadeMode(param->type0.tsp.texture_shading_instr);
  surf.ignore_tex_alpha = param->type0.tsp.ignore_tex_alpha;

  // override a few surface parameters based on the list type
  if (list_type_ != TA_LIST_TRANSLUCENT &&
      list_type_ != TA_LIST_TRANSLUCENT_MODVOL) {
    surf.src_blend = BLEND_NONE;
    surf.dst_blend = BLEND_NONE;
  } else if ((list_type_ == TA_LIST_TRANSLUCENT ||
              list_type_ == TA_LIST_TRANSLUCENT_MODVOL) &&
             tctx.autosort) {
    surf.depth_func = DEPTH_LEQUAL;
  } else if (list_type_ == TA_LIST_PUNCH_THROUGH) {
    surf.depth_func = DEPTH_GEQUAL;
  }

  surf.texture = param->type0.pcw.texture
                     ? GetTexture(tctx, param->type0.tsp, param->type0.tcw)
                     : 0;
}

void TileRenderer::ParseVertexParam(const TileContext &tctx,
                                    TileRenderContext *rctx,
                                    const uint8_t *data) {
  const VertexParam *param = reinterpret_cast<const VertexParam *>(data);
  // If there is no need to change the Global Parameters, a Vertex Parameter for
  // the next polygon may be input immediately after inputting a Vertex
  // Parameter for which "End of Strip" was specified.
  if (last_vertex_ && last_vertex_->type0.pcw.end_of_strip) {
    AllocSurf(rctx, true);
  }
  last_vertex_ = param;

  switch (vertex_type_) {
    case 0: {
      Vertex &vert = AllocVert(rctx);
      vert.xyz[0] = param->type0.xyz[0];
      vert.xyz[1] = param->type0.xyz[1];
      vert.xyz[2] = param->type0.xyz[2];
      ParseColor(param->type0.base_color, &vert.color);
      vert.offset_color = 0;
      vert.uv[0] = 0.0f;
      vert.uv[1] = 0.0f;
    } break;

    case 1: {
      Vertex &vert = AllocVert(rctx);
      vert.xyz[0] = param->type1.xyz[0];
      vert.xyz[1] = param->type1.xyz[1];
      vert.xyz[2] = param->type1.xyz[2];
      ParseColor(param->type1.base_color_r, param->type1.base_color_g,
                 param->type1.base_color_b, param->type1.base_color_a,
                 &vert.color);
      vert.offset_color = 0;
      vert.uv[0] = 0.0f;
      vert.uv[1] = 0.0f;
    } break;

    case 2: {
      Vertex &vert = AllocVert(rctx);
      vert.xyz[0] = param->type2.xyz[0];
      vert.xyz[1] = param->type2.xyz[1];
      vert.xyz[2] = param->type2.xyz[2];
      ParseColor(param->type2.base_intensity, &vert.color);
      vert.offset_color = 0;
      vert.uv[0] = 0.0f;
      vert.uv[1] = 0.0f;
    } break;

    case 3: {
      Vertex &vert = AllocVert(rctx);
      vert.xyz[0] = param->type3.xyz[0];
      vert.xyz[1] = param->type3.xyz[1];
      vert.xyz[2] = param->type3.xyz[2];
      ParseColor(param->type3.base_color, &vert.color);
      ParseOffsetColor(param->type3.offset_color, &vert.offset_color);
      vert.uv[0] = param->type3.uv[0];
      vert.uv[1] = param->type3.uv[1];
    } break;

    case 4: {
      Vertex &vert = AllocVert(rctx);
      vert.xyz[0] = param->type4.xyz[0];
      vert.xyz[1] = param->type4.xyz[1];
      vert.xyz[2] = param->type4.xyz[2];
      ParseColor(param->type4.base_color, &vert.color);
      ParseOffsetColor(param->type4.offset_color, &vert.offset_color);
      uint32_t u = param->type4.uv[0] << 16;
      uint32_t v = param->type4.uv[0] << 16;
      vert.uv[0] = re::load<float>(&u);
      vert.uv[1] = re::load<float>(&v);
    } break;

    case 5: {
      Vertex &vert = AllocVert(rctx);
      vert.xyz[0] = param->type5.xyz[0];
      vert.xyz[1] = param->type5.xyz[1];
      vert.xyz[2] = param->type5.xyz[2];
      ParseColor(param->type5.base_color_r, param->type5.base_color_g,
                 param->type5.base_color_b, param->type5.base_color_a,
                 &vert.color);
      ParseOffsetColor(param->type5.offset_color_r, param->type5.offset_color_g,
                       param->type5.offset_color_b, param->type5.offset_color_a,
                       &vert.offset_color);
      vert.uv[0] = param->type5.uv[0];
      vert.uv[1] = param->type5.uv[1];
    } break;

    case 6: {
      Vertex &vert = AllocVert(rctx);
      vert.xyz[0] = param->type6.xyz[0];
      vert.xyz[1] = param->type6.xyz[1];
      vert.xyz[2] = param->type6.xyz[2];
      ParseColor(param->type6.base_color_r, param->type6.base_color_g,
                 param->type6.base_color_b, param->type6.base_color_a,
                 &vert.color);
      ParseOffsetColor(param->type6.offset_color_r, param->type6.offset_color_g,
                       param->type6.offset_color_b, param->type6.offset_color_a,
                       &vert.offset_color);
      uint32_t u = param->type6.uv[0] << 16;
      uint32_t v = param->type6.uv[0] << 16;
      vert.uv[0] = re::load<float>(&u);
      vert.uv[1] = re::load<float>(&v);
    } break;

    case 7: {
      Vertex &vert = AllocVert(rctx);
      vert.xyz[0] = param->type7.xyz[0];
      vert.xyz[1] = param->type7.xyz[1];
      vert.xyz[2] = param->type7.xyz[2];
      ParseColor(param->type7.base_intensity, &vert.color);
      ParseOffsetColor(param->type7.offset_intensity, &vert.offset_color);
      vert.uv[0] = param->type7.uv[0];
      vert.uv[1] = param->type7.uv[1];
    } break;

    case 8: {
      Vertex &vert = AllocVert(rctx);
      vert.xyz[0] = param->type8.xyz[0];
      vert.xyz[1] = param->type8.xyz[1];
      vert.xyz[2] = param->type8.xyz[2];
      ParseColor(param->type8.base_intensity, &vert.color);
      ParseOffsetColor(param->type8.offset_intensity, &vert.offset_color);
      uint32_t u = param->type8.uv[0] << 16;
      uint32_t v = param->type8.uv[0] << 16;
      vert.uv[0] = re::load<float>(&u);
      vert.uv[1] = re::load<float>(&v);
    } break;

    case 15: {
      auto ParseSpriteVert = [&](int i, Vertex &vert) {
        // FIXME this is assuming all sprites are billboards
        // z isn't specified for i == 3
        vert.xyz[0] = param->sprite0.xyz[i][0];
        vert.xyz[1] = param->sprite0.xyz[i][1];
        vert.xyz[2] = param->sprite0.xyz[0][2];

        ParseColor(face_color_[0], face_color_[1], face_color_[2],
                   face_color_[3], &vert.color);
        ParseOffsetColor(face_offset_color_[0], face_offset_color_[1],
                         face_offset_color_[2], face_offset_color_[3],
                         &vert.offset_color);
      };

      ParseSpriteVert(0, AllocVert(rctx));
      ParseSpriteVert(1, AllocVert(rctx));
      ParseSpriteVert(3, AllocVert(rctx));
      ParseSpriteVert(2, AllocVert(rctx));
    } break;

    case 16: {
      auto ParseSpriteVert = [&](int i, Vertex &vert) {
        // FIXME this is assuming all sprites are billboards
        // z isn't specified for i == 3
        vert.xyz[0] = param->sprite1.xyz[i][0];
        vert.xyz[1] = param->sprite1.xyz[i][1];
        vert.xyz[2] = param->sprite1.xyz[0][2];
        ParseColor(face_color_[0], face_color_[1], face_color_[2],
                   face_color_[3], &vert.color);
        ParseOffsetColor(face_offset_color_[0], face_offset_color_[1],
                         face_offset_color_[2], face_offset_color_[3],
                         &vert.offset_color);
        uint32_t u, v;
        if (i == 3) {
          u = (param->sprite1.uv[0] & 0xffff0000);
          v = (param->sprite1.uv[2] & 0x0000ffff) << 16;
        } else {
          u = (param->sprite1.uv[i] & 0xffff0000);
          v = (param->sprite1.uv[i] & 0x0000ffff) << 16;
        }
        vert.uv[0] = re::load<float>(&u);
        vert.uv[1] = re::load<float>(&v);
      };

      ParseSpriteVert(0, AllocVert(rctx));
      ParseSpriteVert(1, AllocVert(rctx));
      ParseSpriteVert(3, AllocVert(rctx));
      ParseSpriteVert(2, AllocVert(rctx));
    } break;

    case 17: {
      LOG_WARNING("Unhandled modvol triangle");
    } break;

    default:
      LOG_FATAL("Unsupported vertex type %d", vertex_type_);
      break;
  }

  // In the case of the Polygon type, the last Vertex Parameter for an object
  // must have "End of Strip" specified.  If Vertex Parameters with the "End of
  // Strip" specification were not input, but parameters other than the Vertex
  // Parameters were input, the polygon data in question is ignored and an
  // interrupt signal is output.
  // FIXME is this true for sprites which come through this path as well?
}

void TileRenderer::ParseEndOfList(const TileContext &tctx,
                                  TileRenderContext *rctx,
                                  const uint8_t *data) {
  DiscardIncompleteSurf(rctx);

  auto &surfs = rctx->surfs;
  auto &verts = rctx->verts;
  auto &sorted_surfs = rctx->sorted_surfs;

  int first_surf_to_sort = last_sorted_surf_;
  int num_surfs_to_sort = surfs.size() - last_sorted_surf_;

  // sort transparent polys by their z value, from back to front. remember, in
  // dreamcast coordinates smaller z values are further away from the camera
  if ((list_type_ == TA_LIST_TRANSLUCENT ||
       list_type_ == TA_LIST_TRANSLUCENT_MODVOL) &&
      tctx.autosort) {
    int *first = &sorted_surfs[first_surf_to_sort];
    int *last = &sorted_surfs[first_surf_to_sort + num_surfs_to_sort];

    // input order must be preserved in the case minza == minzb
    std::stable_sort(first, last, [&](int a, int b) {
      Surface &surfa = surfs[a];
      Surface &surfb = surfs[b];

      float minza = std::numeric_limits<float>::max();
      for (int i = 0, n = surfa.num_verts; i < n; i++) {
        Vertex &v = verts[surfa.first_vert + i];
        if (v.xyz[2] < minza) {
          minza = v.xyz[2];
        }
      }
      float minzb = std::numeric_limits<float>::max();
      for (int i = 0, n = surfb.num_verts; i < n; i++) {
        Vertex &v = verts[surfb.first_vert + i];
        if (v.xyz[2] < minzb) {
          minzb = v.xyz[2];
        }
      }

      return minza < minzb;
    });
  }

  last_poly_ = nullptr;
  last_vertex_ = nullptr;
  last_sorted_surf_ = surfs.size();
}

// Vertices coming into the TA are in window space, with the Z component being
// 1/W. These coordinates need to be converted back to clip space in order to
// be rendered with OpenGL, etc. While we want to perform an orthographic
// projection on the vertices as they're already perspective correct, the
// renderer backend will have to deal with setting the W component of each
// in order to perspective correct the texture mapping.
void TileRenderer::FillProjectionMatrix(const TileContext &tctx,
                                        TileRenderContext *rctx) {
  auto &verts = rctx->verts;
  float znear = std::numeric_limits<float>::min();
  float zfar = std::numeric_limits<float>::max();

  // Z component is 1/W, so +Z is into the screen
  for (int i = 0, n = verts.size(); i < n; i++) {
    Vertex &v = verts[i];
    if (v.xyz[2] > znear) {
      znear = v.xyz[2];
    }
    if (v.xyz[2] < zfar) {
      zfar = v.xyz[2];
    }
  }

  // fudge so Z isn't being mapped to exactly 0.0 and 1.0
  float zdepth = (znear - zfar) * 1.1f;

  // fix case where a single polygon being renderered
  if (zdepth <= 0.0f) {
    zdepth = 1.0f;
  }

  // convert from window space coordinates into clip space
  rctx->projection = Eigen::Matrix4f::Identity();
  rctx->projection(0, 0) = 2.0f / (float)tctx.video_width;
  rctx->projection(1, 1) = -2.0f / (float)tctx.video_height;
  rctx->projection(0, 3) = -1.0f;
  rctx->projection(1, 3) = 1.0f;
  rctx->projection(2, 2) = (-znear - zfar) / zdepth;
  rctx->projection(2, 3) = (2.0f * zfar * znear) / zdepth;
}

TextureHandle TileRenderer::RegisterTexture(const TileContext &tctx,
                                            const TSP &tsp, const TCW &tcw,
                                            const uint8_t *palette,
                                            const uint8_t *texture) {
  static uint8_t converted[1024 * 1024 * 4];
  const uint8_t *input = texture;
  const uint8_t *output = texture;

  // textures are either twiddled and vq compressed, twiddled and uncompressed
  // or planar
  bool twiddled = !tcw.scan_order;
  bool compressed = tcw.vq_compressed;
  bool mip_mapped = !tcw.scan_order && tcw.mip_mapped;

  // get texture dimensions
  int width = 8 << tsp.texture_u_size;
  int height = mip_mapped ? width : 8 << tsp.texture_v_size;
  int stride = width;
  if (!twiddled && tcw.stride_select) {
    stride = tctx.stride;
  }

  // FIXME used for texcoords, not width / height of texture
  // if (planar && tcw.stride_select) {
  //   width = tctx.stride << 5;
  // }

  // mipmap textures contain data for 1 x 1 up to width x height. skip to the
  // highest res texture and let the renderer backend generate its own mipmaps
  if (mip_mapped) {
    if (compressed) {
      // for vq compressed textures the offset is only for the index data, the
      // codebook is the same for all levels
      input += compressed_mipmap_offsets[tsp.texture_u_size];
    } else if (tcw.pixel_format == TA_PIXEL_4BPP) {
      input += paletted_4bpp_mipmap_offsets[tsp.texture_u_size];
    } else if (tcw.pixel_format == TA_PIXEL_8BPP) {
      input += paletted_8bpp_mipmap_offsets[tsp.texture_u_size];
    } else {
      input += nonpaletted_mipmap_offsets[tsp.texture_u_size];
    }
  }

  // used by vq compressed textures
  static const int codebook_size = 256 * 8;
  const uint8_t *codebook = texture;
  const uint8_t *index = input + codebook_size;

  PixelFormat pixel_fmt = PXL_INVALID;
  switch (tcw.pixel_format) {
    case TA_PIXEL_1555:
    case TA_PIXEL_RESERVED:
      output = converted;
      pixel_fmt = PXL_RGBA5551;
      if (compressed) {
        PixelConvert::ConvertVQ<ARGB1555, RGBA5551>(
            codebook, index, reinterpret_cast<uint16_t *>(converted), width,
            height);
      } else if (twiddled) {
        PixelConvert::ConvertTwiddled<ARGB1555, RGBA5551>(
            reinterpret_cast<const uint16_t *>(input),
            reinterpret_cast<uint16_t *>(converted), width, height);
      } else {
        PixelConvert::Convert<ARGB1555, RGBA5551>(
            reinterpret_cast<const uint16_t *>(input),
            reinterpret_cast<uint16_t *>(converted), stride, height);
      }
      break;

    case TA_PIXEL_565:
      output = converted;
      pixel_fmt = PXL_RGB565;
      if (compressed) {
        PixelConvert::ConvertVQ<RGB565, RGB565>(
            codebook, index, reinterpret_cast<uint16_t *>(converted), width,
            height);
      } else if (twiddled) {
        PixelConvert::ConvertTwiddled<RGB565, RGB565>(
            reinterpret_cast<const uint16_t *>(input),
            reinterpret_cast<uint16_t *>(converted), width, height);
      } else {
        PixelConvert::Convert<RGB565, RGB565>(
            reinterpret_cast<const uint16_t *>(input),
            reinterpret_cast<uint16_t *>(converted), stride, height);
      }
      break;

    case TA_PIXEL_4444:
      output = converted;
      pixel_fmt = PXL_RGBA4444;
      if (compressed) {
        PixelConvert::ConvertVQ<ARGB4444, RGBA4444>(
            codebook, index, reinterpret_cast<uint16_t *>(converted), width,
            height);
      } else if (twiddled) {
        PixelConvert::ConvertTwiddled<ARGB4444, RGBA4444>(
            reinterpret_cast<const uint16_t *>(input),
            reinterpret_cast<uint16_t *>(converted), width, height);
      } else {
        PixelConvert::Convert<ARGB4444, RGBA4444>(
            reinterpret_cast<const uint16_t *>(input),
            reinterpret_cast<uint16_t *>(converted), stride, height);
      }
      break;

    case TA_PIXEL_4BPP:
      CHECK(!compressed);
      output = converted;
      switch (tctx.pal_pxl_format) {
        case TA_PAL_ARGB4444:
          pixel_fmt = PXL_RGBA4444;
          PixelConvert::ConvertPal4<ARGB4444, RGBA4444>(
              input, reinterpret_cast<uint16_t *>(converted),
              reinterpret_cast<const uint32_t *>(palette), width, height);
          break;

        default:
          LOG_FATAL("Unsupported 4bpp palette pixel format %d",
                    tctx.pal_pxl_format);
          break;
      }
      break;

    case TA_PIXEL_8BPP:
      CHECK(!compressed);
      output = converted;
      switch (tctx.pal_pxl_format) {
        case TA_PAL_ARGB4444:
          pixel_fmt = PXL_RGBA4444;
          PixelConvert::ConvertPal8<ARGB4444, RGBA4444>(
              input, reinterpret_cast<uint16_t *>(converted),
              reinterpret_cast<const uint32_t *>(palette), width, height);
          break;

        case TA_PAL_ARGB8888:
          pixel_fmt = PXL_RGBA8888;
          PixelConvert::ConvertPal8<ARGB8888, RGBA8888>(
              input, reinterpret_cast<uint32_t *>(converted),
              reinterpret_cast<const uint32_t *>(palette), width, height);
          break;

        default:
          LOG_FATAL("Unsupported 8bpp palette pixel format %d",
                    tctx.pal_pxl_format);
          break;
      }
      break;

    default:
      LOG_FATAL("Unsupported tcw pixel format %d", tcw.pixel_format);
      break;
  }

  // ignore trilinear filtering for now
  FilterMode filter = tsp.filter_mode == 0 ? FILTER_NEAREST : FILTER_BILINEAR;
  WrapMode wrap_u = tsp.clamp_u
                        ? WRAP_CLAMP_TO_EDGE
                        : (tsp.flip_u ? WRAP_MIRRORED_REPEAT : WRAP_REPEAT);
  WrapMode wrap_v = tsp.clamp_v
                        ? WRAP_CLAMP_TO_EDGE
                        : (tsp.flip_v ? WRAP_MIRRORED_REPEAT : WRAP_REPEAT);

  TextureHandle handle = rb_.RegisterTexture(pixel_fmt, filter, wrap_u, wrap_v,
                                             mip_mapped, width, height, output);

  if (!handle) {
    LOG_WARNING("Failed to register texture");
    return 0;
  }

  return handle;
}

TextureHandle TileRenderer::GetTexture(const TileContext &tctx, const TSP &tsp,
                                       const TCW &tcw) {
  return texture_provider_.GetTexture(
      tsp, tcw, [&](const uint8_t *palette, const uint8_t *texture) {
        return RegisterTexture(tctx, tsp, tcw, palette, texture);
      });
}
