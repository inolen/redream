#ifndef ARM_H
#define ARM_H

#include "hw/memory.h"

struct arm_s;
struct dreamcast_s;

void arm_suspend(struct arm_s *arm);
void arm_resume(struct arm_s *arm);

struct arm_s *arm_create(struct dreamcast_s *dc);
void arm_destroy(struct arm_s *arm);

AM_DECLARE(arm_data_map);

#ifdef __cplusplus
}
#endif

#endif
