#ifndef IMGUI_H
#define IMGUI_H

#include "host/keycode.h"

#ifdef HAVE_IMGUI
#ifndef IMGUI_IMPLEMENTATION
#define CIMGUI_DEFINE_ENUMS_AND_STRUCTS
#include <cimgui/cimgui.h>
#endif
#endif

struct imgui;
struct render_backend;

struct imgui *imgui_create();
void imgui_destroy(struct imgui *imgui);

void imgui_vid_created(struct imgui *imgui, struct render_backend *r);
void imgui_vid_destroyed(struct imgui *imgui);
void imgui_mousemove(struct imgui *imgui, int x, int y);
int imgui_keydown(struct imgui *imgui, int key, uint16_t value);

void imgui_begin_frame(struct imgui *imgui);
void imgui_end_frame(struct imgui *imgui);

#endif
