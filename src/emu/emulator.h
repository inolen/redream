#ifndef EMULATOR_H
#define EMULATOR_H

struct emu_s;
struct window;

void emu_run(struct emu_s *emu, const char *path);

struct emu_s *emu_create(struct window *window);
void emu_destroy(struct emu_s *emu);

#ifdef __cplusplus
}
#endif

#endif
