#ifndef EMULATOR_H
#define EMULATOR_H

#include "host/keycode.h"

struct emu;
struct host;
struct render_backend;

struct emu *emu_create(struct host *host);
void emu_destroy(struct emu *emu);

void emu_vid_created(struct emu *emu, struct render_backend *r);
void emu_vid_destroyed(struct emu *emu);
void emu_vid_swapped(struct emu *emu);
void emu_keydown(struct emu *emu, int port, enum keycode key, int16_t value);

int emu_load(struct emu *emu, const char *path);
void emu_render_frame(struct emu *emu);

#endif
