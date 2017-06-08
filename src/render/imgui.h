#ifndef IMGUI_H
#define IMGUI_H

#include "keycode.h"

struct imgui;
struct render_backend;

struct imgui *imgui_create(struct render_backend *r);
void imgui_destroy(struct imgui *imgui);

void imgui_mousemove(struct imgui *imgui, int x, int y);
void imgui_keydown(struct imgui *imgui, enum keycode key, int16_t value);

void imgui_update_input(struct imgui *imgui);
void imgui_render(struct imgui *imgui);

#endif
