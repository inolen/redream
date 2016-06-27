#ifndef ARM_H
#define ARM_H

#include "hw/memory.h"

struct arm;
struct dreamcast;

void arm_suspend(struct arm *arm);
void arm_resume(struct arm *arm);

struct arm *arm_create(struct dreamcast *dc);
void arm_destroy(struct arm *arm);

AM_DECLARE(arm_data_map);

#endif
