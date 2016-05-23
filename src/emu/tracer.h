#ifndef TRACER_H
#define TRACER_H

#include <unordered_map>
#include <vector>
#include "hw/holly/tr.h"
#include "hw/holly/trace.h"
#include "ui/window.h"

struct window_s;

namespace re {
namespace emu {

struct TextureInst {
  hw::holly::tsp_t tsp;
  hw::holly::tcw_t tcw;
  const uint8_t *palette;
  const uint8_t *texture;
  texture_handle_t handle;
  pxl_format_t format;
  filter_mode_t filter;
  wrap_mode_t wrap_u;
  wrap_mode_t wrap_v;
  bool mipmaps;
  int width;
  int height;
};

typedef std::unordered_map<hw::holly::texture_key_t, TextureInst> TextureMap;

class TraceTextureCache : public hw::holly::TextureProvider {
 public:
  const TextureMap::iterator textures_begin() {
    return textures_.begin();
  }
  const TextureMap::iterator textures_end() {
    return textures_.end();
  }

  void AddTexture(const hw::holly::tsp_t &tsp, hw::holly::tcw_t &tcw,
                  const uint8_t *palette, const uint8_t *texture);
  void RemoveTexture(const hw::holly::tsp_t &tsp, hw::holly::tcw_t &tcw);
  texture_handle_t GetTexture(
      const hw::holly::ta_ctx_t &tctx, const hw::holly::tsp_t &tsp,
      const hw::holly::tcw_t &tcw,
      hw::holly::RegisterTextureDelegate register_delegate);

 private:
  TextureMap textures_;
};

class Tracer : public WindowListener {
 public:
  Tracer(struct window_s &window);
  ~Tracer();

  void Run(const char *path);

 private:
  void OnPaint(bool show_main_menu) final;
  void OnKeyDown(keycode_t code, int16_t value);
  void OnClose() final;

  bool Parse(const char *path);

  void RenderScrubberMenu();
  void RenderTextureMenu();
  void FormatTooltip(int list_type, int vertex_type, int offset);
  void RenderContextMenu();

  void CopyCommandToContext(const hw::holly::TraceCommand *cmd,
                            hw::holly::ta_ctx_t *ctx);
  void PrevContext();
  void NextContext();
  void ResetContext();

  void PrevParam();
  void NextParam();
  void ResetParam();

  struct window_s &window_;
  struct rb_s *rb_;
  TraceTextureCache texcache_;
  hw::holly::TileRenderer tile_renderer_;
  ta_ctx_t tctx_;
  tr_ctx_t rctx_;

  bool running_;
  hw::holly::TraceReader trace_;

  bool hide_params_[hw::holly::TA_NUM_PARAMS];
  int current_frame_, num_frames_;
  hw::holly::TraceCommand *current_cmd_;
  int current_offset_;
  bool scroll_to_param_;
};
}
}

#endif
