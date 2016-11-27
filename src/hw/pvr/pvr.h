#ifndef PVR_H
#define PVR_H

#include <stdint.h>
#include "core/profiler.h"
#include "hw/dreamcast.h"
#include "hw/memory.h"
#include "hw/pvr/pvr_types.h"

struct dreamcast;
struct holly;
struct timer;

struct pvr {
  struct device;
  uint8_t *palette_ram;
  uint8_t *video_ram;
  uint32_t reg[NUM_PVR_REGS];

  /* raster progress */
  struct timer *line_timer;
  int line_clock;
  uint32_t current_line;

#define PVR_REG(offset, name, default, type) type *name;
#include "hw/pvr/pvr_regs.inc"
#undef PVR_REG
};

DECLARE_PROF_STAT(pvr_vblanks);

extern struct reg_cb pvr_cb[NUM_PVR_REGS];

AM_DECLARE(pvr_reg_map);
AM_DECLARE(pvr_vram_map);

struct pvr *pvr_create(struct dreamcast *dc);
void pvr_destroy(struct pvr *pvr);

#endif
