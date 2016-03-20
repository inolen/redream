#ifndef TILE_RENDERER_H
#define TILE_RENDERER_H

#include <functional>
#include <map>
#include <Eigen/Dense>
#include "core/array.h"
#include "renderer/backend.h"

namespace re {
namespace hw {
namespace holly {

union TSP;
union TCW;
union PolyParam;
union VertexParam;
struct TileContext;

// The TextureCache interface provides an abstraction so the TileAccelerator /
// Tracer can provide raw texture and palette data on demand to the
// TileRenderer. While a static GetTextureKey is provided, each implementation
// is expected to manage their own cache internally.
typedef std::function<renderer::TextureHandle(const uint8_t *, const uint8_t *)>
    RegisterTextureCallback;

typedef uint64_t TextureKey;

class TextureProvider {
 public:
  static TextureKey GetTextureKey(const TSP &tsp, const TCW &tcw);

  virtual ~TextureProvider() {}

  virtual renderer::TextureHandle GetTexture(
      const TSP &tsp, const TCW &tcw, RegisterTextureCallback register_cb) = 0;
};

// The TileRenderer class is responsible for taking a particular TileContext,
// parsing it and ultimately rendering it out to the supplied backend. This
// is split out of the main TileAccelerator code so it can be re-used by the
// Tracer.
struct TileRenderContext {
  Eigen::Matrix4f projection;
  re::array<renderer::Surface> surfs;
  re::array<renderer::Vertex> verts;
  re::array<int> sorted_surfs;

  // map tile context offset -> number of surfs / verts rendered
  struct ParamMapEntry {
    int num_surfs;
    int num_verts;
  };
  std::map<int, ParamMapEntry> param_map;
};

class TileRenderer {
 public:
  TileRenderer(renderer::Backend &rb, TextureProvider &texture_provider);

  void ParseContext(const TileContext &tctx, TileRenderContext *rctx,
                    bool map_params);
  void RenderContext(const TileRenderContext &rctx);
  void RenderContext(const TileContext &tctx);

 private:
  void Reset(TileRenderContext *rctx);
  renderer::Surface &AllocSurf(TileRenderContext *rctx, bool copy_from_prev);
  renderer::Vertex &AllocVert(TileRenderContext *rctx);
  void DiscardIncompleteSurf(TileRenderContext *rctx);
  void ParseColor(uint32_t base_color, uint32_t *color);
  void ParseColor(float r, float g, float b, float a, uint32_t *color);
  void ParseColor(float intensity, uint32_t *color);
  void ParseOffsetColor(uint32_t offset_color, uint32_t *color);
  void ParseOffsetColor(float r, float g, float b, float a, uint32_t *color);
  void ParseOffsetColor(float intensity, uint32_t *color);
  void ParseBackground(const TileContext &tctx, TileRenderContext *rctx);
  void ParsePolyParam(const TileContext &tctx, TileRenderContext *rctx,
                      const uint8_t *data);
  void ParseVertexParam(const TileContext &tctx, TileRenderContext *rctx,
                        const uint8_t *data);
  void ParseEndOfList(const TileContext &tctx, TileRenderContext *rctx,
                      const uint8_t *data);
  void FillProjectionMatrix(const TileContext &tctx, TileRenderContext *rctx);

  renderer::TextureHandle RegisterTexture(const TileContext &tctx,
                                          const TSP &tsp, const TCW &tcw,
                                          const uint8_t *palette,
                                          const uint8_t *texture);
  renderer::TextureHandle GetTexture(const TileContext &tctx, const TSP &tsp,
                                     const TCW &tcw);

  renderer::Backend &rb_;
  TextureProvider &texture_provider_;

  // keep a persistent instance of this for RenderContext. the instance will be
  // reset before each use, but the memory claimed by surfs / verts won't have
  // to be reallocated
  TileRenderContext rctx_;

  // current global state
  const PolyParam *last_poly_;
  const VertexParam *last_vertex_;
  int list_type_;
  int vertex_type_;
  float face_color_[4];
  float face_offset_color_[4];
  int last_sorted_surf_;
};
}
}
}

#endif
