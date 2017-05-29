#ifndef GDROM_H
#define GDROM_H

#include <stdint.h>
#include "hw/gdrom/disc.h"

struct dreamcast;
struct gdrom;

struct gdrom *gdrom_create(struct dreamcast *dc);
void gdrom_destroy(struct gdrom *gd);

int gdrom_drive_status(struct gdrom *gd);
int gdrom_disk_format(struct gdrom *gd);

void gdrom_set_disc(struct gdrom *gd, struct disc *disc);
void gdrom_dma_begin(struct gdrom *gd);
int gdrom_dma_read(struct gdrom *gd, uint8_t *data, int n);
void gdrom_dma_end(struct gdrom *gd);

#endif
