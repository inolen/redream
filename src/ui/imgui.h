#ifndef IMGUI_H
#define IMGUI_H

struct window_s;
struct imgui_s;

struct imgui_s *imgui_create(struct window_s *window);
void imgui_destroy(struct imgui_s *imgui);

#endif
