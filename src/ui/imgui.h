#ifndef IMGUI_H
#define IMGUI_H

struct window;
struct imgui;

struct imgui *imgui_create(struct window *window);
void imgui_destroy(struct imgui *imgui);

#endif
