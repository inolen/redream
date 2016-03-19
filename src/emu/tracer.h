#ifndef TRACER_H
#define TRACER_H

#include <unordered_map>
#include <vector>
#include "hw/holly/tile_renderer.h"
#include "hw/holly/trace.h"
#include "ui/window_listener.h"

namespace re {

namespace ui {
class Window;
}

namespace emu {

struct TextureInst {
  hw::holly::TSP tsp;
  hw::holly::TCW tcw;
  const uint8_t *palette;
  const uint8_t *texture;
  renderer::TextureHandle handle;
};

typedef std::unordered_map<hw::holly::TextureKey, TextureInst> TextureMap;

class TraceTextureCache : public hw::holly::TextureProvider {
 public:
  const TextureMap::iterator textures_begin() { return textures_.begin(); }
  const TextureMap::iterator textures_end() { return textures_.end(); }

  void AddTexture(const hw::holly::TSP &tsp, hw::holly::TCW &tcw,
                  const uint8_t *palette, const uint8_t *texture);
  void RemoveTexture(const hw::holly::TSP &tsp, hw::holly::TCW &tcw);
  renderer::TextureHandle GetTexture(
      const hw::holly::TSP &tsp, const hw::holly::TCW &tcw,
      hw::holly::RegisterTextureCallback register_cb);
  renderer::TextureHandle GetTexture(hw::holly::TextureKey texture_key);

 private:
  TextureMap textures_;
};

class Tracer : public ui::WindowListener {
 public:
  Tracer(ui::Window &window);
  ~Tracer();

  void Run(const char *path);

 private:
  void OnPaint(bool show_main_menu) final;
  void OnKeyDown(ui::Keycode code, int16_t value);
  void OnClose() final;

  bool Parse(const char *path);
  int GetNumFrames();
  void SetFrame(int n);
  void CopyCommandToContext(const hw::holly::TraceCommand *cmd,
                            hw::holly::TileContext *ctx);

  ui::Window &window_;
  TraceTextureCache texcache_;
  hw::holly::TileRenderer tile_renderer_;

  bool running_;
  hw::holly::TraceReader reader_;
  hw::holly::TraceCommand *current_cmd_;
  hw::holly::TileContext current_ctx_;
  hw::holly::TextureKey current_tex_;
  int num_frames_;
};
}
}

#endif
