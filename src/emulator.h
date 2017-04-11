#ifndef EMULATOR_H
#define EMULATOR_H

struct emu;
struct window;

void emu_run(struct emu *emu, const char *path);

struct emu *emu_create(struct window *window);
void emu_destroy(struct emu *emu);

#endif
