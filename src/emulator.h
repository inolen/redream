#ifndef EMULATOR_H
#define EMULATOR_H

struct emu;
struct host;
struct render_backend;

struct emu *emu_create(struct host *host, struct render_backend *r);
void emu_destroy(struct emu *emu);

int emu_load_game(struct emu *emu, const char *path);

void emu_render_frame(struct emu *emu, int width, int height);

#endif
