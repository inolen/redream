#ifndef BIOS_H
#define BIOS_H

struct dreamcast;
struct bios;

struct bios *bios_create(struct dreamcast *dc);
void bios_destroy(struct bios *bios);

int bios_init(struct bios *bios);
void bios_debug_menu(struct bios *bios);

#endif
