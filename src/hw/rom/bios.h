#ifndef BIOS_H
#define BIOS_H

#include "memory.h"

struct dreamcast;
struct bios;

AM_DECLARE(boot_rom_map);

struct bios *bios_create(struct dreamcast *dc);
void bios_destroy(struct bios *bios);

#endif
