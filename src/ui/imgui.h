#ifndef IMGUI_H
#define IMGUI_H

#ifdef __cplusplus
extern "C" {
#endif

struct window_s;
struct imgui_s;

struct imgui_s *imgui_create(struct window_s *window);
void imgui_destroy(struct imgui_s *imgui);

#ifdef __cplusplus
}
#endif

#endif
