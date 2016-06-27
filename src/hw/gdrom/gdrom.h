#ifndef GDROM_H
#define GDROM_H

#include <stdint.h>
#include "hw/gdrom/disc.h"

struct dreamcast;
struct gdrom;

void gdrom_set_disc(struct gdrom *gd, struct disc *disc);
void gdrom_dma_begin(struct gdrom *gd);
int gdrom_dma_read(struct gdrom *gd, uint8_t *data, int data_size);
void gdrom_dma_end(struct gdrom *gd);

struct gdrom *gdrom_create(struct dreamcast *dc);
void gdrom_destroy(struct gdrom *gd);

#endif
