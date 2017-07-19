#ifndef IMGUI_H
#define IMGUI_H

#include "host/keycode.h"

#ifdef HAVE_IMGUI
#define CIMGUI_DEFINE_ENUMS_AND_STRUCTS
#include <cimgui/cimgui.h>
#endif

struct imgui;
struct render_backend;

struct imgui *imgui_create(struct render_backend *r);
void imgui_destroy(struct imgui *imgui);

void imgui_mousemove(struct imgui *imgui, int x, int y);
void imgui_keydown(struct imgui *imgui, enum keycode key, int16_t value);

void imgui_begin_frame(struct imgui *imgui, int width, int height);
void imgui_render(struct imgui *imgui);

#endif
