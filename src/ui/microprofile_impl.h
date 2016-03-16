#ifndef MICROPROFILE_IMPL_H
#define MICROPROFILE_IMPL_H

#include "renderer/backend.h"
#include "ui/window_listener.h"

namespace re {
namespace ui {

static const int MAX_2D_VERTICES = 16384;
static const int MAX_2D_SURFACES = 256;

class Window;

class MicroProfileImpl : public WindowListener {
 public:
  MicroProfileImpl(Window &window);
  ~MicroProfileImpl();

  bool Init();

  void DrawText(int x, int y, uint32_t color, const char *text);
  void DrawBox(int x0, int y0, int x1, int y1, uint32_t color,
               renderer::BoxType type);
  void DrawLine(float *verts, int num_verts, uint32_t color);

 private:
  void OnPostPaint() final;
  void OnKeyDown(Keycode code, int16_t value) final;
  void OnMouseMove(int x, int y) final;

  renderer::Vertex2D *AllocVertices(const renderer::Surface2D &desc, int count);

  Window &window_;
  renderer::TextureHandle font_tex_;
  renderer::Surface2D surfs_[MAX_2D_SURFACES];
  int num_surfs_;
  renderer::Vertex2D verts_[MAX_2D_VERTICES];
  int num_verts_;
};
}
}

#endif
