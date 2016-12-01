#ifndef ARM7_H
#define ARM7_H

#include "core/profiler.h"
#include "hw/memory.h"

struct arm7;
struct dreamcast;

enum arm7_interrupt {
  ARM7_INT_FIQ = 0x1,
};

DECLARE_COUNTER(arm7_instrs);

AM_DECLARE(arm7_data_map);

struct arm7 *arm7_create(struct dreamcast *dc);
void arm7_destroy(struct arm7 *arm);
void arm7_suspend(struct arm7 *arm);
void arm7_reset(struct arm7 *arm);
void arm7_check_interrupts(struct arm7 *arm);
void arm7_raise_interrupt(struct arm7 *arm, enum arm7_interrupt intr);

#endif
