#ifndef FLASH_H
#define FLASH_H

#include "hw/memory.h"

struct dreamcast;
struct flash;

struct flash *flash_create(struct dreamcast *dc);
void flash_destroy(struct flash *flash);

AM_DECLARE(flash_rom_map);

#endif
