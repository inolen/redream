#ifndef BOOT_H
#define BOOT_H

#include "hw/memory.h"

struct dreamcast;
struct boot;

struct boot *boot_create(struct dreamcast *dc);
void boot_destroy(struct boot *boot);

AM_DECLARE(boot_rom_map);

#endif
