#ifndef EMULATOR_H
#define EMULATOR_H

#ifdef __cplusplus
extern "C" {
#endif

struct emu_s;
struct window_s;

void emu_run(struct emu_s *emu, const char *path);

struct emu_s *emu_create(struct window_s *window);
void emu_destroy(struct emu_s *emu);

#ifdef __cplusplus
}
#endif

#endif
