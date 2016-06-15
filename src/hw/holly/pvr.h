#ifndef PVR_H
#define PVR_H

#include <stdint.h>
#include "hw/holly/pvr_types.h"
#include "hw/dreamcast.h"
#include "hw/memory.h"
#include "hw/scheduler.h"

#ifdef __cplusplus
extern "C" {
#endif

struct dreamcast_s;
struct holly_s;

typedef struct pvr_s {
  device_t base;

  struct scheduler_s *scheduler;
  struct holly_s *holly;
  address_space_t *space;

  uint8_t *palette_ram;
  uint8_t *video_ram;
  uint32_t reg[NUM_PVR_REGS];
  void *reg_data[NUM_PVR_REGS];
  reg_read_cb reg_read[NUM_PVR_REGS];
  reg_write_cb reg_write[NUM_PVR_REGS];
  struct timer_s *line_timer;
  int line_clock;
  uint32_t current_scanline;

#define PVR_REG(offset, name, default, type) type *name;
#include "hw/holly/pvr_regs.inc"
#undef PVR_REG
} pvr_t;

pvr_t *pvr_create(struct dreamcast_s *dc);
void pvr_destroy(pvr_t *pvr);

AM_DECLARE(pvr_reg_map);
AM_DECLARE(pvr_vram_map);

#ifdef __cplusplus
}
#endif

#endif
