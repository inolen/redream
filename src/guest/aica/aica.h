#ifndef AICA_H
#define AICA_H

#include <stdint.h>

struct aica;
struct dreamcast;

#define AICA_SAMPLE_FREQ 44100

struct aica *aica_create(struct dreamcast *dc);
void aica_destroy(struct aica *aica);

void aica_debug_menu(struct aica *aica);

void aica_set_clock(struct aica *aica, uint32_t time);

uint32_t aica_mem_read(struct aica *aica, uint32_t addr, uint32_t mask);
void aica_mem_write(struct aica *aica, uint32_t addr, uint32_t data,
                    uint32_t mask);

uint32_t aica_reg_read(struct aica *aica, uint32_t addr, uint32_t mask);
void aica_reg_write(struct aica *aica, uint32_t addr, uint32_t data,
                    uint32_t mask);

#endif
