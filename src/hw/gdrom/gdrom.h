#ifndef GDROM_H
#define GDROM_H

#include <stdint.h>
#include "hw/gdrom/disc.h"

#ifdef __cplusplus
extern "C" {
#endif

struct dreamcast_s;
struct gdrom_s;

void gdrom_set_disc(struct gdrom_s *gd, struct disc_s *disc);
void gdrom_dma_begin(struct gdrom_s *gd);
int gdrom_dma_read(struct gdrom_s *gd, uint8_t *data, int data_size);
void gdrom_dma_end(struct gdrom_s *gd);

struct gdrom_s *gdrom_create(struct dreamcast_s *dc);
void gdrom_destroy(struct gdrom_s *gd);

#ifdef __cplusplus
}
#endif

#endif
