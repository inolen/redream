#ifndef NUKLEAR_H
#define NUKLEAR_H

#include <stdbool.h>

#define NK_INCLUDE_FIXED_TYPES
#define NK_INCLUDE_STANDARD_IO
#define NK_INCLUDE_DEFAULT_ALLOCATOR
#define NK_INCLUDE_VERTEX_BUFFER_OUTPUT
#define NK_INCLUDE_FONT_BAKING
#define NK_INCLUDE_DEFAULT_FONT
#include <nuklear.h>

#include "renderer/backend.h"

struct window;
struct window_listener;

#define NK_MAX_VERTICES 4096
#define NK_MAX_ELEMENTS 16384

struct nuklear {
  struct window *window;
  struct window_listener *listener;

  //
  struct nk_context ctx;
  struct nk_buffer cmds;
  struct nk_font_atlas atlas;
  struct nk_draw_null_texture null;

  // render buffers
  struct vertex2d vertices[NK_MAX_VERTICES];
  uint16_t elements[NK_MAX_ELEMENTS];

  // input state
  int mousex, mousey;
  int mouse_wheel;
  bool mouse_down[3];
  bool alt[2];
  bool ctrl[2];
  bool shift[2];
};

struct nuklear *nuklear_create(struct window *window);
void nuklear_destroy(struct nuklear *nk);

#endif
