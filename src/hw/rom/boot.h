#ifndef BOOT_H
#define BOOT_H

#include "memory.h"

struct dreamcast;
struct boot;

AM_DECLARE(boot_rom_map);

struct boot *boot_create(struct dreamcast *dc);
void boot_destroy(struct boot *boot);

#endif
