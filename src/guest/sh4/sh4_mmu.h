#ifndef SH4_MMU_H
#define SH4_MMU_H

#include "guest/sh4/sh4_types.h"

struct sh4_tlb_entry {
  union pteh hi;
  union ptel lo;
};

void sh4_mmu_ltlb(struct sh4 *sh4);
uint32_t sh4_mmu_itlb_read(struct sh4 *sh4, uint32_t addr, uint32_t mask);
uint32_t sh4_mmu_utlb_read(struct sh4 *sh4, uint32_t addr, uint32_t mask);
void sh4_mmu_itlb_write(struct sh4 *sh4, uint32_t addr, uint32_t data,
                        uint32_t mask);
void sh4_mmu_utlb_write(struct sh4 *sh4, uint32_t addr, uint32_t data,
                        uint32_t mask);

#endif
