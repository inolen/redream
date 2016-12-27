#ifndef NUKLEAR_H
#define NUKLEAR_H

#include <stdbool.h>

#define NK_INCLUDE_FIXED_TYPES
#define NK_INCLUDE_STANDARD_IO
#define NK_INCLUDE_STANDARD_VARARGS
#define NK_INCLUDE_DEFAULT_ALLOCATOR
#define NK_INCLUDE_VERTEX_BUFFER_OUTPUT
#define NK_INCLUDE_FONT_BAKING
#define NK_INCLUDE_DEFAULT_FONT
#include <nuklear.h>

#include "ui/window.h"
#include "video/render_backend.h"

#define NK_MAX_VERTICES 16384
#define NK_MAX_ELEMENTS (NK_MAX_VERTICES * 4)

struct nuklear {
  struct window *window;
  struct window_listener listener;

  struct nk_context ctx;
  struct nk_buffer cmds;
  struct nk_font_atlas atlas;
  struct nk_draw_null_texture null;
  texture_handle_t font_texture;

  /* render buffers */
  struct vertex2d vertices[NK_MAX_VERTICES];
  uint16_t elements[NK_MAX_ELEMENTS];

  /* input state */
  int mousex, mousey;
  int mouse_wheel;
  bool mouse_down[3];
  bool alt[2];
  bool ctrl[2];
  bool shift[2];
};

struct nuklear *nk_create(struct window *window);
void nk_destroy(struct nuklear *nk);

void nk_begin_frame(struct nuklear *nk);
void nk_end_frame(struct nuklear *nk);

#endif
