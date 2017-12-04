#ifndef BOOT_H
#define BOOT_H

#include <stdint.h>

struct dreamcast;
struct boot;

struct boot *boot_create(struct dreamcast *dc);
void boot_destroy(struct boot *boot);

uint32_t boot_rom_read(struct boot *boot, uint32_t addr, uint32_t mask);
void boot_rom_write(struct boot *boot, uint32_t addr, uint32_t data,
                    uint32_t mask);

#endif
