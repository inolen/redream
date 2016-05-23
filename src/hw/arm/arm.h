#ifndef ARM_H
#define ARM_H

#include "hw/memory.h"

#ifdef __cplusplus
extern "C" {
#endif

struct arm_s;
struct dreamcast_s;

AM_DECLARE(arm_data_map);

struct arm_s *arm_create(struct dreamcast_s *dc);
void arm_destroy(struct arm_s *arm);

void arm_suspend(struct arm_s *arm);
void arm_resume(struct arm_s *arm);

#ifdef __cplusplus
}
#endif

#endif
