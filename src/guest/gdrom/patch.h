#ifndef PATCH_H
#define PATCH_H

#include <stdint.h>

struct address_space;

enum {
  PATCH_BOOTFILE = 0x1,
  PATCH_WIDESCREEN = 0x2,
};

struct patch_hunk {
  int offset;
  uint8_t *data;
  int len;
};

struct patch {
  char *game;
  char *desc;
  int flags;
  struct patch_hunk *hunks;
  int num_hunks;
};

void patch_debug_menu();

int patch_widescreen_enabled(const char *game);

void patch_bootfile(const char *game, uint8_t *data, int offset, int size);

#endif
