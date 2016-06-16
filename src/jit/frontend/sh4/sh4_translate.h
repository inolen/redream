#ifndef SH4_BUILDER_H
#define SH4_BUILDER_H

struct ir_s;

void sh4_translate(uint32_t guest_addr, uint8_t *guest_ptr, int size, int flags,
                   struct ir_s *ir);

#endif
