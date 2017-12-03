#ifndef GDROM_H
#define GDROM_H

#include "guest/gdrom/disc.h"
#include "guest/gdrom/gdrom_types.h"

struct dreamcast;
struct gdrom;

struct gdrom *gdrom_create(struct dreamcast *dc);
void gdrom_destroy(struct gdrom *gd);

void gdrom_debug_menu(struct gdrom *gd);

struct disc *gdrom_get_disc(struct gdrom *gd);
void gdrom_set_disc(struct gdrom *gd, struct disc *disc);

void gdrom_dma_begin(struct gdrom *gd);
int gdrom_dma_read(struct gdrom *gd, uint8_t *data, int n);
void gdrom_dma_end(struct gdrom *gd);

int gdrom_is_busy(struct gdrom *gd);
void gdrom_get_mode(struct gdrom *gd, struct gd_hw_info *info);
void gdrom_set_mode(struct gdrom *gd, struct gd_hw_info *info);
void gdrom_get_status(struct gdrom *gd, struct gd_status_info *stat);
void gdrom_get_error(struct gdrom *gd, struct gd_error_info *err);
void gdrom_get_toc(struct gdrom *gd, int area, struct gd_toc_info *toc);
void gdrom_get_session(struct gdrom *gd, int session,
                       struct gd_session_info *ses);
void gdrom_get_subcode(struct gdrom *gd, int format, uint8_t *data, int size);

void gdrom_get_bootfile(struct gdrom *gd, int *fad, int *len);

int gdrom_find_file(struct gdrom *gd, const char *filename, int *fad, int *len);
int gdrom_read_sectors(struct gdrom *gd, int fad, int num_sectors, int fmt,
                       int mask, uint8_t *dst, int dst_size);
int gdrom_read_bytes(struct gdrom *gd, int fad, int len, uint8_t *dst,
                     int dst_size);

#endif
