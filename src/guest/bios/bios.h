#ifndef BIOS_H
#define BIOS_H

#include <stdint.h>

struct dreamcast;

struct bios {
  struct dreamcast *dc;

  /* gdrom state */
  uint32_t status;
  uint32_t cmd_id;
  uint32_t cmd_code;
  uint32_t params[4];
  uint32_t result[4];
};

struct bios *bios_create(struct dreamcast *dc);
void bios_destroy(struct bios *bios);

int bios_init(struct bios *bios);
int bios_invalid_instr(struct bios *bios);

void bios_debug_menu(struct bios *bios);

#endif
