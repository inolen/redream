#ifndef PVR_H
#define PVR_H

#include "guest/dreamcast.h"
#include "guest/pvr/pvr_types.h"

struct dreamcast;
struct holly;
struct timer;

#define PVR_FRAMEBUFFER_SIZE 640 * 640 * 4

struct pvr {
  struct device;
  uint8_t *vram;
  uint32_t reg[PVR_NUM_REGS];

  /* raster progress */
  struct timer *line_timer;
  int line_clock;
  uint32_t current_line;

  /* copy of deinterlaced framebuffer from texture memory */
  uint8_t framebuffer[PVR_FRAMEBUFFER_SIZE];
  int framebuffer_w;
  int framebuffer_h;

  /* tracks if a STARTRENDER was received for the current frame */
  int got_startrender;

#define PVR_REG(offset, name, default, type) type *name;
#include "guest/pvr/pvr_regs.inc"
#undef PVR_REG
};

struct pvr *pvr_create(struct dreamcast *dc);
void pvr_destroy(struct pvr *pvr);

void pvr_video_size(struct pvr *pvr, int *video_width, int *video_height);

uint32_t pvr_reg_read(struct pvr *pvr, uint32_t addr, uint32_t mask);
void pvr_reg_write(struct pvr *pvr, uint32_t addr, uint32_t data,
                   uint32_t mask);

uint32_t pvr_vram64_read(struct pvr *pvr, uint32_t addr, uint32_t mask);
void pvr_vram64_write(struct pvr *pvr, uint32_t addr, uint32_t data,
                      uint32_t mask);

uint32_t pvr_vram32_read(struct pvr *pvr, uint32_t addr, uint32_t mask);
void pvr_vram32_write(struct pvr *pvr, uint32_t addr, uint32_t data,
                      uint32_t mask);

#endif
