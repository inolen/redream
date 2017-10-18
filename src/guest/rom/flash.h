#ifndef FLASHROM_H
#define FLASHROM_H

#include <stdint.h>

struct dreamcast;
struct flash;

struct flash *flash_create(struct dreamcast *dc);
void flash_destroy(struct flash *flash);

uint32_t flash_rom_read(struct flash *flash, uint32_t addr, uint32_t mask);
void flash_rom_write(struct flash *flash, uint32_t addr, uint32_t data,
                     uint32_t mask);

void flash_read(struct flash *flash, int offset, void *data, int n);
void flash_write(struct flash *flash, int offset, const void *data, int n);
void flash_program(struct flash *flash, int offset, const void *data, int n);
void flash_erase(struct flash *flash, int offset, int n);

#endif
