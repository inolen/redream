#ifndef PVR_H
#define PVR_H

#include <stdint.h>
#include "hw/dreamcast.h"
#include "hw/memory.h"
#include "hw/pvr/pvr_types.h"
#include "hw/scheduler.h"

struct dreamcast;
struct holly;

struct pvr {
  struct device;
  uint8_t *palette_ram;
  uint8_t *video_ram;
  uint32_t reg[NUM_PVR_REGS];

  /* raster progress */
  struct timer *line_timer;
  int line_clock;
  uint32_t current_line;

  /* perf */
  int64_t last_vbs_time;
  int vbs;
  int num_vblanks;

#define PVR_REG(offset, name, default, type) type *name;
#include "hw/pvr/pvr_regs.inc"
#undef PVR_REG
};

extern struct reg_cb pvr_cb[NUM_PVR_REGS];

void pvr_destroy(struct pvr *pvr);
struct pvr *pvr_create(struct dreamcast *dc);

AM_DECLARE(pvr_reg_map);
AM_DECLARE(pvr_vram_map);

#endif
