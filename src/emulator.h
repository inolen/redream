#ifndef EMULATOR_H
#define EMULATOR_H

struct emu;
struct host;

struct emu *emu_create(struct host *host);
void emu_destroy(struct emu *emu);

int emu_load_game(struct emu *emu, const char *path);

void emu_run_frame(struct emu *emu);

#endif
