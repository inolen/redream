#ifndef IMGUI_H
#define IMGUI_H

/* imgui extensions */
#if HAVE_IMGUI

#ifndef IMGUI_IMPLEMENTATION
#define CIMGUI_DEFINE_ENUMS_AND_STRUCTS
#include <cimgui/cimgui.h>
#endif
#include "host/keycode.h"

enum {
  IMFONT_OSWALD_MEDIUM,
  IMFONT_OPENSANS_REGULAR,
  IMFONT_NUM_FONTS,
};

/* note, any icon defined through IMICON_* must be listed here */
#define IMICON_RANGES                                                     \
  0xf00d, 0xf00d, 0xf2db, 0xf2db, 0xf067, 0xf067, 0xf07c, 0xf07c, 0xf0a0, \
      0xf0a0, 0xf108, 0xf108, 0xf11b, 0xf11b

#define IMICON_TIMES u8"\uf00d"
#define IMICON_MICROCHIP u8"\uf2db"
#define IMICON_PLUS u8"\uf067"
#define IMICON_FOLDER_OPEN u8"\uf07c"
#define IMICON_HDD u8"\uf0a0"
#define IMICON_DESKTOP u8"\uf108"
#define IMICON_GAMEPAD u8"\uf11b"

void igPushFontEx(int id, int font_height);
int igTab(const char *label, struct ImVec2 size, int selected);
int igOptionInt(const char *label, int value, struct ImVec2 size);
int igOptionString(const char *label, const char *value, struct ImVec2 size);
int igDiscButton(ImTextureID user_texture_id, float item_diameter,
                 float draw_diameter, const struct ImVec2 uv0,
                 const struct ImVec2 uv1);

#endif

/* imgui implementation */
struct imgui;
struct render_backend;

struct imgui *imgui_create();
void imgui_destroy(struct imgui *imgui);

void imgui_vid_created(struct imgui *imgui, struct render_backend *r);
void imgui_vid_destroyed(struct imgui *imgui);
void imgui_mousemove(struct imgui *imgui, int x, int y);
int imgui_keydown(struct imgui *imgui, int key, int16_t value);

void imgui_begin_frame(struct imgui *imgui);
void imgui_end_frame(struct imgui *imgui);

#endif
