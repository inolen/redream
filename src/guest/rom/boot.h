#ifndef BOOT_H
#define BOOT_H

#include "guest/memory.h"

struct dreamcast;
struct boot;

AM_DECLARE(boot_rom_map);

struct boot *boot_create(struct dreamcast *dc);
void boot_destroy(struct boot *boot);

void boot_read(struct boot *boot, int offset, void *data, int n);
void boot_write(struct boot *boot, int offset, const void *data, int n);

#endif
