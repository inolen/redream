#include "holly/pixel_convert.h"
#include "holly/tile_accelerator.h"
#include "holly/tile_renderer.h"

using namespace dreavm::holly;
using namespace dreavm::renderer;

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
      BLEND_SRC_COLOR, BLEND_ONE_MINUS_SRC_COLOR,
      BLEND_SRC_ALPHA, BLEND_ONE_MINUS_SRC_ALPHA,
      BLEND_DST_ALPHA, BLEND_ONE_MINUS_DST_ALPHA};
  return src_blend_funcs[blend_func];
}

inline BlendFunc TranslateDstBlendFunc(uint32_t blend_func) {
  static BlendFunc dst_blend_funcs[] = {
      BLEND_ZERO,      BLEND_ONE,
      BLEND_DST_COLOR, BLEND_ONE_MINUS_DST_COLOR,
      BLEND_SRC_ALPHA, BLEND_ONE_MINUS_SRC_ALPHA,
      BLEND_DST_ALPHA, BLEND_ONE_MINUS_DST_ALPHA};
  return dst_blend_funcs[blend_func];
}

inline ShadeMode TranslateShadeMode(uint32_t shade_mode) {
  static ShadeMode shade_modes[] = {SHADE_DECAL, SHADE_MODULATE,
                                    SHADE_DECAL_ALPHA, SHADE_MODULATE_ALPHA};
  return shade_modes[shade_mode];
}

TileRenderer::TileRenderer(TextureCache &texcache) : texcache_(texcache) {}

void TileRenderer::RenderContext(const TileContext *tactx, Backend *rb) {
  const uint8_t *data = tactx->data;
  const uint8_t *end = tactx->data + tactx->size;

  ResetState();

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
        last_poly_ = nullptr;
        last_vertex_ = nullptr;
        break;

      case TA_PARAM_OBJ_LIST_SET:
        LOG(FATAL) << "TA_PARAM_OBJ_LIST_SET unsupported";
        break;

      // global params
      case TA_PARAM_POLY_OR_VOL:
        ParsePolyParam(tactx, rb, reinterpret_cast<const PolyParam *>(data));
        break;

      case TA_PARAM_SPRITE:
        ParsePolyParam(tactx, rb, reinterpret_cast<const PolyParam *>(data));
        break;

      // vertex params
      case TA_PARAM_VERTEX:
        ParseVertexParam(tactx, rb,
                         reinterpret_cast<const VertexParam *>(data));
        break;

      default:
        LOG(FATAL) << "Unhandled";
        break;
    }

    data += TileAccelerator::GetParamSize(pcw, vertex_type_);
  }

  NormalizeZ();

  // LOG(INFO) << "StartRender " << num_surfs_ << " surfs, "
  //           << num_verts << " verts, " << tactx->size << " bytes";

  rb->BindFramebuffer(FB_TILE_ACELLERATOR);
  rb->Clear(0.1f, 0.39f, 0.88f, 1.0f);
  rb->RenderSurfaces(surfs_, num_surfs_, verts_, num_verts_, sorted_surfs_);
}

void TileRenderer::ResetState() {
  // reset global state
  last_poly_ = nullptr;
  last_vertex_ = nullptr;
  list_type_ = 0;
  vertex_type_ = 0;

  // reset render state
  num_surfs_ = 0;
  num_verts_ = 0;
  last_sorted_surf_ = 0;
}

Surface *TileRenderer::AllocSurf() {
  CHECK_LT(num_surfs_, MAX_SURFACES);

  // reuse previous surface if it wasn't completed, else, allocate a new one
  int id;
  if (last_vertex_ && !last_vertex_->type0.pcw.end_of_strip) {
    id = num_surfs_ - 1;
  } else {
    id = num_surfs_++;
  }

  // reset the surface
  Surface *surf = &surfs_[id];
  surf->first_vert = num_verts_;
  surf->num_verts = 0;

  // default sort the surface
  sorted_surfs_[id] = id;

  return surf;
}

Vertex *TileRenderer::AllocVert() {
  CHECK_LT(num_verts_, MAX_VERTICES);
  Surface *surf = &surfs_[num_surfs_ - 1];
  surf->num_verts++;
  return &verts_[num_verts_++];
}

// FIXME we could offload a lot of this to the GPU, generating shaders
// for different combinations of ISP/TSP parameters once the logic is
// ironed out
// FIXME honor use alpha
// FIXME honor ignore tex alpha
void TileRenderer::ParseColor(uint32_t base_color, float *color) {
  color[0] = ((base_color >> 16) & 0xff) / 255.0f;
  color[1] = ((base_color >> 8) & 0xff) / 255.0f;
  color[2] = (base_color & 0xff) / 255.0f;
  color[3] = ((base_color >> 24) & 0xff) / 255.0f;
}

void TileRenderer::ParseColor(float r, float g, float b, float a,
                              float *color) {
  color[0] = r;
  color[1] = g;
  color[2] = b;
  color[3] = a;
}

void TileRenderer::ParseColor(float intensity, float *color) {
  color[0] = face_color_[0] * intensity;
  color[1] = face_color_[1] * intensity;
  color[2] = face_color_[2] * intensity;
  color[3] = face_color_[3];
}

void TileRenderer::ParseOffsetColor(uint32_t offset_color, float *color) {
  if (!last_poly_->type0.isp_tsp.offset) {
    memset(color, 0, sizeof(float) * 4);
  } else {
    color[0] = ((offset_color >> 16) & 0xff) / 255.0f;
    color[1] = ((offset_color >> 8) & 0xff) / 255.0f;
    color[2] = (offset_color & 0xff) / 255.0f;
    color[3] = ((offset_color >> 24) & 0xff) / 255.0f;
  }
}

void TileRenderer::ParseOffsetColor(float r, float g, float b, float a,
                                    float *color) {
  if (!last_poly_->type0.isp_tsp.offset) {
    memset(color, 0, sizeof(float) * 4);
  } else {
    color[0] = r;
    color[1] = g;
    color[2] = b;
    color[3] = a;
  }
}

void TileRenderer::ParseOffsetColor(float intensity, float *color) {
  if (!last_poly_->type0.isp_tsp.offset) {
    memset(color, 0, sizeof(float) * 4);
  } else {
    color[0] = face_offset_color_[0] * intensity;
    color[1] = face_offset_color_[1] * intensity;
    color[2] = face_offset_color_[2] * intensity;
    color[3] = face_offset_color_[3];
  }
}

void TileRenderer::ParseBackground(const TileContext *tactx) {
  // translate the surface
  Surface *surf = AllocSurf();
  surf->texture = 0;
  surf->depth_write = !tactx->bg_isp.z_write_disable;
  surf->depth_func = TranslateDepthFunc(tactx->bg_isp.depth_compare_mode);
  surf->cull = TranslateCull(tactx->bg_isp.culling_mode);
  surf->src_blend = BLEND_NONE;
  surf->dst_blend = BLEND_NONE;

  // translate the first 3 vertices
  Vertex *verts[4] = {nullptr};
  int offset = 0;
  for (int i = 0; i < 3; i++) {
    Vertex *v = verts[i] = AllocVert();

    v->xyz[0] = *reinterpret_cast<const float *>(&tactx->bg_vertices[offset]);
    v->xyz[1] =
        *reinterpret_cast<const float *>(&tactx->bg_vertices[offset + 4]);
    v->xyz[2] =
        *reinterpret_cast<const float *>(&tactx->bg_vertices[offset + 8]);
    offset += 12;

    if (tactx->bg_isp.texture) {
      LOG(FATAL) << "Unhandled";
      // v->uv[0] = *reinterpret_cast<const float
      // *>(&tactx->bg_vertices[offset]);
      // v->uv[1] = *reinterpret_cast<const float *>(&tactx->bg_vertices[offset
      // + 4]);
      // offset += 8;
    }

    uint32_t base_color =
        *reinterpret_cast<const uint32_t *>(&tactx->bg_vertices[offset]);
    v->color[0] = ((base_color >> 16) & 0xff) / 255.0f;
    v->color[1] = ((base_color >> 8) & 0xff) / 255.0f;
    v->color[2] = (base_color & 0xff) / 255.0f;
    v->color[3] = ((base_color >> 24) & 0xff) / 255.0f;
    offset += 4;

    if (tactx->bg_isp.offset) {
      LOG(FATAL) << "Unhandled";
      // uint32_t offset_color = *reinterpret_cast<const uint32_t
      // *>(&tactx->bg_vertices[offset]);
      // v->offset_color[0] = ((offset_color >> 16) & 0xff) / 255.0f;
      // v->offset_color[1] = ((offset_color >> 16) & 0xff) / 255.0f;
      // v->offset_color[2] = ((offset_color >> 16) & 0xff) / 255.0f;
      // v->offset_color[3] = 0.0f;
      // offset += 4;
    }
  }

  // 4th vertex isn't supplied, fill it out automatically
  verts[3] = AllocVert();
  verts[3]->xyz[0] = verts[1]->xyz[0];
  verts[3]->xyz[1] = verts[2]->xyz[1];
  verts[3]->xyz[2] = verts[0]->xyz[2];
  verts[3]->color[0] = verts[0]->color[0];
  verts[3]->color[1] = verts[0]->color[1];
  verts[3]->color[2] = verts[0]->color[2];
  verts[3]->color[3] = verts[0]->color[3];
  verts[3]->uv[0] = verts[1]->uv[0];
  verts[3]->uv[1] = verts[2]->uv[1];
}

// NOTE this offset color implementation is not correct at all
//      see the Texture/Shading Instruction in the TSP instruction word
// ALSO check out 16bit uv flag
void TileRenderer::ParsePolyParam(const TileContext *tactx,
                                  renderer::Backend *rb,
                                  const PolyParam *param) {
  last_poly_ = param;
  last_vertex_ = nullptr;
  list_type_ = param->type0.pcw.list_type;
  vertex_type_ = TileAccelerator::GetVertexType(param->type0.pcw);
  face_color_[0] = face_color_[1] = face_color_[2] = face_color_[3] = 0.0f;
  face_offset_color_[0] = face_offset_color_[1] = face_offset_color_[2] =
      face_offset_color_[3] = 0.0f;

  // setup the new surface
  Surface *surf = AllocSurf();
  surf->depth_write = !param->type0.isp_tsp.z_write_disable;
  surf->depth_func =
      TranslateDepthFunc(param->type0.isp_tsp.depth_compare_mode);
  surf->cull = TranslateCull(param->type0.isp_tsp.culling_mode);
  surf->src_blend = TranslateSrcBlendFunc(param->type0.tsp.src_alpha_instr);
  surf->dst_blend = TranslateDstBlendFunc(param->type0.tsp.dst_alpha_instr);
  surf->shade = TranslateShadeMode(param->type0.tsp.texture_shading_instr);

  // override a few surface parameters based on the list type
  if (list_type_ != TA_LIST_TRANSLUCENT &&
      list_type_ != TA_LIST_TRANSLUCENT_MODVOL) {
    surf->src_blend = BLEND_NONE;
    surf->dst_blend = BLEND_NONE;
  } else if (list_type_ == TA_LIST_TRANSLUCENT &&
             list_type_ == TA_LIST_TRANSLUCENT_MODVOL && tactx->autosort) {
    surf->depth_func = DEPTH_LEQUAL;
  } else if (list_type_ == TA_LIST_PUNCH_THROUGH) {
    surf->depth_func = DEPTH_GEQUAL;
  }

  surf->texture =
      param->type0.pcw.texture
          ? GetTexture(tactx, rb, param->type0.tsp, param->type0.tcw)
          : 0;

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

    case 6: {
      // don't do anything with modifier volume yet
      num_surfs_--;
    } break;

    default:
      LOG(FATAL) << "Unhandled";
      break;
  }
}

void TileRenderer::ParseVertexParam(const TileContext *tactx,
                                    renderer::Backend *rb,
                                    const VertexParam *param) {
  // If there is no need to change the Global Parameters, a Vertex Parameter for
  // the next polygon may be input immediately after inputting a Vertex
  // Parameter for which "End of Strip" was specified.
  if (last_vertex_ && last_vertex_->type0.pcw.end_of_strip) {
    // start a new surface
    ParsePolyParam(tactx, rb, last_poly_);
  }
  last_vertex_ = param;

  switch (vertex_type_) {
    case 0: {
      Vertex *vert = AllocVert();
      vert->xyz[0] = param->type0.xyz[0];
      vert->xyz[1] = param->type0.xyz[1];
      vert->xyz[2] = param->type0.xyz[2];
      ParseColor(param->type0.base_color, vert->color);
      vert->offset_color[1] = 0.0f;
      vert->offset_color[2] = 0.0f;
      vert->offset_color[3] = 0.0f;
      vert->uv[0] = 0.0f;
      vert->uv[1] = 0.0f;
    } break;

    case 1: {
      Vertex *vert = AllocVert();
      vert->xyz[0] = param->type1.xyz[0];
      vert->xyz[1] = param->type1.xyz[1];
      vert->xyz[2] = param->type1.xyz[2];
      ParseColor(param->type1.base_color_r, param->type1.base_color_g,
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
      Vertex *vert = AllocVert();
      vert->xyz[0] = param->type2.xyz[0];
      vert->xyz[1] = param->type2.xyz[1];
      vert->xyz[2] = param->type2.xyz[2];
      ParseColor(param->type2.base_intensity, vert->color);
      vert->offset_color[0] = 0.0f;
      vert->offset_color[1] = 0.0f;
      vert->offset_color[2] = 0.0f;
      vert->offset_color[3] = 0.0f;
      vert->uv[0] = 0.0f;
      vert->uv[1] = 0.0f;
    } break;

    case 3: {
      Vertex *vert = AllocVert();
      vert->xyz[0] = param->type3.xyz[0];
      vert->xyz[1] = param->type3.xyz[1];
      vert->xyz[2] = param->type3.xyz[2];
      ParseColor(param->type3.base_color, vert->color);
      ParseOffsetColor(param->type3.offset_color, vert->offset_color);
      vert->uv[0] = param->type3.uv[0];
      vert->uv[1] = param->type3.uv[1];
    } break;

    case 4: {
      Vertex *vert = AllocVert();
      vert->xyz[0] = param->type4.xyz[0];
      vert->xyz[1] = param->type4.xyz[1];
      vert->xyz[2] = param->type4.xyz[2];
      ParseColor(param->type4.base_color, vert->color);
      ParseOffsetColor(param->type4.offset_color, vert->offset_color);
      uint32_t u = param->type4.uv[0] << 16;
      uint32_t v = param->type4.uv[0] << 16;
      vert->uv[0] = *reinterpret_cast<float *>(&u);
      vert->uv[1] = *reinterpret_cast<float *>(&v);
    } break;

    case 5: {
      Vertex *vert = AllocVert();
      vert->xyz[0] = param->type5.xyz[0];
      vert->xyz[1] = param->type5.xyz[1];
      vert->xyz[2] = param->type5.xyz[2];
      ParseColor(param->type5.base_color_r, param->type5.base_color_g,
                 param->type5.base_color_b, param->type5.base_color_a,
                 vert->color);
      ParseOffsetColor(param->type5.offset_color_r, param->type5.offset_color_g,
                       param->type5.offset_color_b, param->type5.offset_color_a,
                       vert->offset_color);
      vert->uv[0] = param->type5.uv[0];
      vert->uv[1] = param->type5.uv[1];
    } break;

    case 6: {
      Vertex *vert = AllocVert();
      vert->xyz[0] = param->type6.xyz[0];
      vert->xyz[1] = param->type6.xyz[1];
      vert->xyz[2] = param->type6.xyz[2];
      ParseColor(param->type6.base_color_r, param->type6.base_color_g,
                 param->type6.base_color_b, param->type6.base_color_a,
                 vert->color);
      ParseOffsetColor(param->type6.offset_color_r, param->type6.offset_color_g,
                       param->type6.offset_color_b, param->type6.offset_color_a,
                       vert->offset_color);
      uint32_t u = param->type6.uv[0] << 16;
      uint32_t v = param->type6.uv[0] << 16;
      vert->uv[0] = *reinterpret_cast<float *>(&u);
      vert->uv[1] = *reinterpret_cast<float *>(&v);
    } break;

    case 7: {
      Vertex *vert = AllocVert();
      vert->xyz[0] = param->type7.xyz[0];
      vert->xyz[1] = param->type7.xyz[1];
      vert->xyz[2] = param->type7.xyz[2];
      ParseColor(param->type7.base_intensity, vert->color);
      ParseOffsetColor(param->type7.offset_intensity, vert->offset_color);
      vert->uv[0] = param->type7.uv[0];
      vert->uv[1] = param->type7.uv[1];
    } break;

    case 8: {
      Vertex *vert = AllocVert();
      vert->xyz[0] = param->type8.xyz[0];
      vert->xyz[1] = param->type8.xyz[1];
      vert->xyz[2] = param->type8.xyz[2];
      ParseColor(param->type8.base_intensity, vert->color);
      ParseOffsetColor(param->type8.offset_intensity, vert->offset_color);
      uint32_t u = param->type8.uv[0] << 16;
      uint32_t v = param->type8.uv[0] << 16;
      vert->uv[0] = *reinterpret_cast<float *>(&u);
      vert->uv[1] = *reinterpret_cast<float *>(&v);
    } break;

    case 15:
      LOG(FATAL) << "Unhandled";
      break;

    case 16: {
      CHECK_EQ(param->sprite1.pcw.end_of_strip, 1);

      auto ParseSpriteVert = [&](int i, Vertex *vert) {
        vert->xyz[0] = param->sprite1.xyz[i][0];
        vert->xyz[1] = param->sprite1.xyz[i][1];
        // FIXME this is assuming all sprites are billboards
        // z isn't specified for i == 3
        vert->xyz[2] = param->sprite1.xyz[0][2];
        ParseColor(1.0f, 1.0f, 1.0f, 1.0f, vert->color);
        ParseOffsetColor(1.0f, 1.0f, 1.0f, 1.0f, vert->offset_color);
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

      ParseSpriteVert(0, AllocVert());
      ParseSpriteVert(1, AllocVert());
      ParseSpriteVert(3, AllocVert());
      ParseSpriteVert(2, AllocVert());
    } break;

    case 17: {
    } break;

    default:
      LOG(FATAL) << "Unsupported vertex type " << vertex_type_;
      break;
  }

  // In the case of the Polygon type, the last Vertex Parameter for an object
  // must have "End of Strip" specified.  If Vertex Parameters with the "End of
  // Strip" specification were not input, but parameters other than the Vertex
  // Parameters were input, the polygon data in question is ignored and an
  // interrupt signal is output.
  // FIXME is this true for sprites which come through this path as well?
}

void TileRenderer::ParseEndOfList(const TileContext *tactx) {
  int first_surf_to_sort = last_sorted_surf_;
  int num_surfs_to_sort = num_surfs_ - last_sorted_surf_;

  // sort transparent polys by their z value, from back to front. remember, in
  // dreamcast coordinates smaller z values are further away from the camera
  if ((list_type_ == TA_LIST_TRANSLUCENT ||
       list_type_ == TA_LIST_TRANSLUCENT_MODVOL) &&
      tactx->autosort) {
    int *first = sorted_surfs_ + first_surf_to_sort;
    int *last = sorted_surfs_ + first_surf_to_sort + num_surfs_to_sort;
    std::sort(first, last, [&](int a, int b) {
      Surface *surfa = &surfs_[a];
      Surface *surfb = &surfs_[b];

      float minza = std::numeric_limits<float>::max();
      for (int i = 0; i < surfa->num_verts; i++) {
        Vertex *v = &verts_[surfa->first_vert + i];
        if (v->xyz[2] < minza) {
          minza = v->xyz[2];
        }
      }
      float minzb = std::numeric_limits<float>::max();
      for (int i = 0; i < surfb->num_verts; i++) {
        Vertex *v = &verts_[surfb->first_vert + i];
        if (v->xyz[2] < minzb) {
          minzb = v->xyz[2];
        }
      }

      return minza < minzb;
    });
  }

  last_poly_ = nullptr;
  last_vertex_ = nullptr;
  last_sorted_surf_ = num_surfs_;
}

// Vertices coming into the TA are already in window space. However, the Z
// component of these vertices isn't normalized to a particular range, but
// they do sort correctly. In order to render the vertices correctly, the
// Z components need to be normalized between 0.0 and 1.0.
void TileRenderer::NormalizeZ() {
  float znear = std::numeric_limits<float>::min();
  float zfar = std::numeric_limits<float>::max();

  // get the bounds of the Z components
  for (int i = 0; i < num_verts_; i++) {
    Vertex *v = &verts_[i];
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
  for (int i = 0; i < num_verts_; i++) {
    Vertex *v = &verts_[i];
    v->xyz[2] = 0.99f - ((v->xyz[2] - zfar) / zdelta) * 0.99f;
  }
}

TextureHandle TileRenderer::RegisterTexture(const TileContext *tactx,
                                            Backend *rb, const TSP &tsp,
                                            const TCW &tcw,
                                            const uint8_t *texture,
                                            const uint8_t *palette) {
  static uint8_t converted[1024 * 1024 * 4];
  const uint8_t *buffer = texture;

  // textures are either twiddled and vq compressed, twiddled and uncompressed
  // or planar
  bool twiddled = !tcw.scan_order;
  bool compressed = tcw.vq_compressed;

  // get texture dimensions
  int width = 8 << tsp.texture_u_size;
  int height = 8 << tsp.texture_v_size;
  if (!twiddled && tcw.mip_mapped) {
    height = width;
  }
  int stride = width;
  if (!twiddled && tcw.stride_select) {
    stride = tactx->stride;
  }

  // FIXME used for texcoords, not width / height of texture
  // if (planar && tcw.stride_select) {
  //   width = tactx->stride << 5;
  // }

  PixelFormat pixel_fmt;
  switch (tcw.pixel_format) {
    case TA_PIXEL_1555:
      buffer = converted;
      pixel_fmt = PXL_RGBA5551;
      if (compressed) {
        PixelConvert::ConvertVQ<ARGB1555, RGBA5551>(
            texture, (uint16_t *)converted, width, height);
      } else if (twiddled) {
        PixelConvert::ConvertTwiddled<ARGB1555, RGBA5551>(
            (uint16_t *)texture, (uint16_t *)converted, width, height);
      } else {
        PixelConvert::Convert<ARGB1555, RGBA5551>(
            (uint16_t *)texture, (uint16_t *)converted, stride, height);
      }
      break;

    case TA_PIXEL_565:
      buffer = converted;
      pixel_fmt = PXL_RGB565;
      if (compressed) {
        PixelConvert::ConvertVQ<RGB565, RGB565>(texture, (uint16_t *)converted,
                                                width, height);
      } else if (twiddled) {
        PixelConvert::ConvertTwiddled<RGB565, RGB565>(
            (uint16_t *)texture, (uint16_t *)converted, width, height);
      } else {
        PixelConvert::Convert<RGB565, RGB565>(
            (uint16_t *)texture, (uint16_t *)converted, stride, height);
      }
      break;

    case TA_PIXEL_4444:
      buffer = converted;
      pixel_fmt = PXL_RGBA4444;
      if (compressed) {
        PixelConvert::ConvertVQ<ARGB4444, RGBA4444>(
            texture, (uint16_t *)converted, width, height);
      } else if (twiddled) {
        PixelConvert::ConvertTwiddled<ARGB4444, RGBA4444>(
            (uint16_t *)texture, (uint16_t *)converted, width, height);
      } else {
        PixelConvert::Convert<ARGB4444, RGBA4444>(
            (uint16_t *)texture, (uint16_t *)converted, stride, height);
      }
      break;

    case TA_PIXEL_4BPP:
      buffer = converted;
      switch (tactx->pal_pxl_format) {
        case TA_PAL_ARGB1555:
          pixel_fmt = PXL_RGBA5551;
          LOG(FATAL) << "Unhandled";
          break;

        case TA_PAL_RGB565:
          pixel_fmt = PXL_RGB565;
          LOG(FATAL) << "Unhandled";
          break;

        case TA_PAL_ARGB4444:
          CHECK_EQ(false, twiddled);
          pixel_fmt = PXL_RGBA4444;
          PixelConvert::ConvertPal4<ARGB4444, RGBA4444>(
              texture, (uint16_t *)converted, (uint32_t *)palette, width,
              height);
          break;

        case TA_PAL_ARGB8888:
          CHECK_EQ(true, twiddled);
          pixel_fmt = PXL_RGBA8888;
          LOG(FATAL) << "Unhandled";
          break;
      }
      break;

    case TA_PIXEL_8BPP:
      buffer = converted;
      switch (tactx->pal_pxl_format) {
        case TA_PAL_ARGB1555:
          pixel_fmt = PXL_RGBA5551;
          LOG(FATAL) << "Unhandled";
          break;

        case TA_PAL_RGB565:
          pixel_fmt = PXL_RGB565;
          LOG(FATAL) << "Unhandled";
          break;

        case TA_PAL_ARGB4444:
          CHECK_EQ(true, twiddled);
          pixel_fmt = PXL_RGBA4444;
          PixelConvert::ConvertPal8<ARGB4444, RGBA4444>(
              texture, (uint16_t *)converted, (uint32_t *)palette, width,
              height);
          break;

        case TA_PAL_ARGB8888:
          CHECK_EQ(true, twiddled);
          pixel_fmt = PXL_RGBA8888;
          PixelConvert::ConvertPal8<ARGB8888, RGBA8888>(
              texture, (uint32_t *)converted, (uint32_t *)palette, width,
              height);
          break;
      }
      break;

    default:
      LOG(FATAL) << "Unsupported tcw pixel format " << tcw.pixel_format;
      break;
  }

  // ignore trilinear filtering for now
  FilterMode filter = tsp.filter_mode == 0 ? FILTER_NEAREST : FILTER_BILINEAR;

  TextureHandle handle =
      rb->RegisterTexture(pixel_fmt, filter, width, height, buffer);
  if (!handle) {
    LOG(WARNING) << "Failed to register texture";
    return 0;
  }

  return handle;
}

TextureHandle TileRenderer::GetTexture(const TileContext *tactx, Backend *rb,
                                       const TSP &tsp, const TCW &tcw) {
  return texcache_.GetTexture(
      tsp, tcw, [&](const uint8_t *texture, const uint8_t *palette) {
        return RegisterTexture(tactx, rb, tsp, tcw, texture, palette);
      });
}
