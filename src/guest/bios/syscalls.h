#ifndef SYSCALLS_H
#define SYSCALLS_H

struct bios;

void bios_fontrom_vector(struct bios *bios);
void bios_sysinfo_vector(struct bios *bios);
void bios_flashrom_vector(struct bios *bios);
void bios_gdrom_vector(struct bios *bios);
void bios_system_vector(struct bios *bios);

#endif
