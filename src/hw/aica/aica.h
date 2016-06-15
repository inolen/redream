#ifndef AICA_H
#define AICA_H

#include <stdint.h>
#include "hw/memory.h"

#ifdef __cplusplus
extern "C" {
#endif

struct dreamcast_s;
struct aica_s;

struct aica_s *aica_create(struct dreamcast_s *dc);
void aica_destroy(struct aica_s *aica);

AM_DECLARE(aica_reg_map);
AM_DECLARE(aica_data_map);

#ifdef __cplusplus
}
#endif

#endif
