#ifndef GDROM_H
#define GDROM_H

#include "guest/gdrom/disc.h"
#include "guest/gdrom/gdrom_types.h"

struct dreamcast;
struct gdrom;

struct gdrom *gdrom_create(struct dreamcast *dc);
void gdrom_destroy(struct gdrom *gd);

void gdrom_set_disc(struct gdrom *gd, struct disc *disc);
void gdrom_dma_begin(struct gdrom *gd);
int gdrom_dma_read(struct gdrom *gd, uint8_t *data, int n);
void gdrom_dma_end(struct gdrom *gd);

void gdrom_get_disc_id(struct gdrom *gd, char *id, int size);
void gdrom_get_drive_mode(struct gdrom *gd, struct gd_hw_info *info);
void gdrom_set_drive_mode(struct gdrom *gd, struct gd_hw_info *info);
void gdrom_get_status(struct gdrom *gd, struct gd_spi_status *stat);
void gdrom_get_error(struct gdrom *gd, struct gd_spi_error *err);
void gdrom_get_toc(struct gdrom *gd, int area, struct gd_spi_toc *toc);
void gdrom_get_session(struct gdrom *gd, int session,
                       struct gd_spi_session *ses);
void gdrom_get_subcode(struct gdrom *gd, int format, uint8_t *data, int size);
int gdrom_find_file(struct gdrom *gd, const char *filename, int *fad, int *len);
int gdrom_read_sectors(struct gdrom *gd, int fad, int num_sectors, int fmt,
                       int mask, uint8_t *dst, int dst_size);
int gdrom_read_bytes(struct gdrom *gd, int fad, int len, uint8_t *dst,
                     int dst_size);

#endif
