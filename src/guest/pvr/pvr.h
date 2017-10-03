#ifndef PVR_H
#define PVR_H

#include "guest/dreamcast.h"
#include "guest/memory.h"
#include "guest/pvr/pvr_types.h"

struct dreamcast;
struct holly;
struct timer;

struct pvr {
  struct device;
  uint8_t *palette_ram;
  uint8_t *video_ram;
  uint32_t reg[PVR_NUM_REGS];

  /* raster progress */
  struct timer *line_timer;
  int line_clock;
  uint32_t current_line;

#define PVR_REG(offset, name, default, type) type *name;
#include "guest/pvr/pvr_regs.inc"
#undef PVR_REG
};

AM_DECLARE(pvr_reg_map);
AM_DECLARE(pvr_vram_map);

extern struct reg_cb pvr_cb[PVR_NUM_REGS];

struct pvr *pvr_create(struct dreamcast *dc);
void pvr_destroy(struct pvr *pvr);

#endif
