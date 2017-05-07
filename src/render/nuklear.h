#ifndef NUKLEAR_H
#define NUKLEAR_H

#define NK_INCLUDE_FIXED_TYPES
#define NK_INCLUDE_STANDARD_IO
#define NK_INCLUDE_STANDARD_VARARGS
#define NK_INCLUDE_DEFAULT_ALLOCATOR
#define NK_INCLUDE_VERTEX_BUFFER_OUTPUT
#define NK_INCLUDE_FONT_BAKING
#define NK_INCLUDE_DEFAULT_FONT
#include <nuklear.h>

#include "keycode.h"
#include "render/render_backend.h"

#define NK_MAX_VERTICES 16384
#define NK_MAX_ELEMENTS (NK_MAX_VERTICES * 4)

#define DEBUG_MENU_HEIGHT 23.0f

struct render_backend;

struct nuklear {
  struct render_backend *r;

  struct nk_context ctx;
  struct nk_buffer cmds;
  struct nk_font_atlas atlas;
  struct nk_draw_null_texture null;
  texture_handle_t font_texture;

  /* render buffers */
  struct vertex2 vertices[NK_MAX_VERTICES];
  uint16_t elements[NK_MAX_ELEMENTS];

  /* input state */
  int mousex, mousey;
  int mouse_wheel;
  int mouse_down[3];
  int alt[2];
  int ctrl[2];
  int shift[2];
};

struct nuklear *nk_create(struct render_backend *r);
void nk_destroy(struct nuklear *nk);

void nk_mousemove(struct nuklear *nk, int x, int y);
void nk_keydown(struct nuklear *nk, enum keycode key, int16_t value);

void nk_update_input(struct nuklear *nk);
void nk_render(struct nuklear *nk);

#endif
