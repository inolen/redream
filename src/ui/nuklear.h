#ifndef NUKLEAR_H
#define NUKLEAR_H

#define NK_INCLUDE_FIXED_TYPES
#define NK_INCLUDE_STANDARD_IO
#define NK_INCLUDE_DEFAULT_ALLOCATOR
#define NK_INCLUDE_VERTEX_BUFFER_OUTPUT
#define NK_INCLUDE_FONT_BAKING
#define NK_INCLUDE_DEFAULT_FONT
#include <nuklear.h>

struct window;
struct window_listener;

struct nuklear {
  struct window *window;
  struct window_listener *listener;
  struct nk_context ctx;
  struct nk_buffer cmds;
  struct nk_font_atlas atlas;
  struct nk_draw_null_texture null;
  bool alt[2];
  bool ctrl[2];
  bool shift[2];
};

struct nuklear *nuklear_create(struct window *window);
void nuklear_destroy(struct nuklear *nk);

#endif
