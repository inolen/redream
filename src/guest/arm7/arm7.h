#ifndef ARM7_H
#define ARM7_H

#include <stdint.h>

struct arm7;
struct dreamcast;

enum arm7_interrupt {
  ARM7_INT_FIQ = 0x1,
};

/* clang-format off */
#define ARM7_AICA_MEM_BEGIN 0x00000000
#define ARM7_AICA_MEM_END   0x001fffff

#define ARM7_AICA_REG_BEGIN 0x00800000
#define ARM7_AICA_REG_END   0x009fffff
/* clang-format on */

struct arm7 *arm7_create(struct dreamcast *dc);
void arm7_destroy(struct arm7 *arm);

void arm7_debug_menu(struct arm7 *arm);
void arm7_suspend(struct arm7 *arm);
void arm7_reset(struct arm7 *arm);
void arm7_raise_interrupt(struct arm7 *arm, enum arm7_interrupt intr);

uint32_t arm7_mem_read(struct arm7 *arm, uint32_t addr, uint32_t mask);
void arm7_mem_write(struct arm7 *arm, uint32_t addr, uint32_t data,
                    uint32_t mask);

#endif
