#ifndef AICA_H
#define AICA_H

#include <stdint.h>
#include "hw/memory.h"

struct aica;
struct dreamcast;

AM_DECLARE(aica_reg_map);
AM_DECLARE(aica_data_map);

struct aica *aica_create(struct dreamcast *dc);
void aica_destroy(struct aica *aica);

#endif
