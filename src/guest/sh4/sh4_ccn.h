#ifndef SH4_CCN_H
#define SH4_CCN_H

void sh4_ccn_pref(struct sh4 *sh4, uint32_t addr);
uint32_t sh4_ccn_cache_read(struct sh4 *sh4, uint32_t addr, uint32_t mask);
void sh4_ccn_cache_write(struct sh4 *sh4, uint32_t addr, uint32_t data,
                         uint32_t mask);
uint32_t sh4_ccn_sq_read(struct sh4 *sh4, uint32_t addr, uint32_t mask);
void sh4_ccn_sq_write(struct sh4 *sh4, uint32_t addr, uint32_t data,
                      uint32_t mask);
uint32_t sh4_ccn_icache_read(struct sh4 *sh4, uint32_t addr, uint32_t mask);
void sh4_ccn_icache_write(struct sh4 *sh4, uint32_t addr, uint32_t data,
                          uint32_t mask);
uint32_t sh4_ccn_ocache_read(struct sh4 *sh4, uint32_t addr, uint32_t mask);
void sh4_ccn_ocache_write(struct sh4 *sh4, uint32_t addr, uint32_t data,
                          uint32_t mask);

#endif
