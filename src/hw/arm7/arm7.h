#ifndef ARM7_H
#define ARM7_H

#include "hw/memory.h"

struct arm7;
struct dreamcast;

void arm7_suspend(struct arm7 *arm);
void arm7_resume(struct arm7 *arm);

struct arm7 *arm7_create(struct dreamcast *dc);
void arm7_destroy(struct arm7 *arm);

AM_DECLARE(arm7_data_map);

#endif
