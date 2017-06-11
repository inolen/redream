#ifndef GDROM_H
#define GDROM_H

#include "hw/gdrom/disc.h"
#include "hw/gdrom/gdrom_types.h"

struct dreamcast;
struct gdrom;

struct gdrom *gdrom_create(struct dreamcast *dc);
void gdrom_destroy(struct gdrom *gd);

void gdrom_set_disc(struct gdrom *gd, struct disc *disc);
void gdrom_dma_begin(struct gdrom *gd);
int gdrom_dma_read(struct gdrom *gd, uint8_t *data, int n);
void gdrom_dma_end(struct gdrom *gd);

void gdrom_get_drive_mode(struct gdrom *gd, struct gd_hw_info *info);
void gdrom_set_drive_mode(struct gdrom *gd, struct gd_hw_info *info);
void gdrom_get_status(struct gdrom *gd, uint8_t *data, int size);
void gdrom_get_error(struct gdrom *gd, uint8_t *data, int size);
void gdrom_get_toc(struct gdrom *gd, enum gd_area area_type, uint8_t *data,
                   int size);
void gdrom_get_session(struct gdrom *gd, int session, uint8_t *data, int size);
void gdrom_get_subcode(struct gdrom *gd, int format, uint8_t *data, int size);
int gdrom_read_sectors(struct gdrom *gd, int fad, enum gd_secfmt fmt,
                       enum gd_secmask mask, int num_sectors, uint8_t *dst,
                       int dst_size);

#endif
