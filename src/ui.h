#ifndef UI_H
#define UI_H

#include "host/keycode.h"

struct ui;
struct host;
struct render_backend;

enum {
  UI_PAGE_NONE = -1,
  UI_PAGE_GAMES = 0,
  UI_PAGE_OPTIONS,
  UI_PAGE_LIBRARY,
  UI_PAGE_SYSTEM,
  UI_PAGE_VIDEO,
  UI_PAGE_INPUT,
  UI_PAGE_CONTROLLERS,
  UI_PAGE_KEYBOARD,
  UI_NUM_PAGES,
};

struct ui *ui_create(struct host *host);
void ui_destroy(struct ui *ui);

void ui_vid_created(struct ui *ui, struct render_backend *r);
void ui_vid_destroyed(struct ui *ui);
void ui_mousemove(struct ui *ui, int x, int y);
int ui_keydown(struct ui *ui, int key, int16_t value);

void ui_build_menus(struct ui *ui);
void ui_set_page(struct ui *ui, int page_index);

#endif
