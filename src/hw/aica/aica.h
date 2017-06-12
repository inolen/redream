#ifndef AICA_H
#define AICA_H

#include "memory.h"

struct aica;
struct dreamcast;

#define AICA_SAMPLE_FREQ 44100

AM_DECLARE(aica_reg_map);
AM_DECLARE(aica_data_map);

struct aica *aica_create(struct dreamcast *dc);
void aica_destroy(struct aica *aica);

void aica_debug_menu(struct aica *aica);

void aica_set_clock(struct aica *aica, uint32_t time);

#endif
