#ifndef FRONTEND_H
#define FRONTEND_H

struct ir_s;
struct jit_frontend_s;

typedef void (*jit_frontend_translate_code)(struct jit_frontend_s *frontend,
                                            uint32_t guest_addr,
                                            uint8_t *guest_ptr, int flags,
                                            int *size, struct ir_s *ir);
typedef void (*jit_frontend_dump_code)(struct jit_frontend_s *frontend,
                                       uint32_t guest_addr, uint8_t *guest_ptr,
                                       int size);

typedef struct jit_frontend_s {
  jit_frontend_translate_code translate_code;
  jit_frontend_dump_code dump_code;
} jit_frontend_t;

#endif
