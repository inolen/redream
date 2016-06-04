#ifndef SH4_ANALYZER_H
#define SH4_ANALYZER_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
  SH4_SLOWMEM = 0x1,
  SH4_DOUBLE_PR = 0x2,
  SH4_DOUBLE_SZ = 0x4,
  SH4_SINGLE_INSTR = 0x8,
};

void sh4_analyze_block(uint32_t guest_addr, uint8_t *guest_ptr, int flags,
                       int *size);

#ifdef __cplusplus
}
#endif

#endif
