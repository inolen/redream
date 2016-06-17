#ifndef FRONTEND_H
#define FRONTEND_H

struct ir;
struct jit_frontend;

typedef void (*jit_frontend_translate_code)(struct jit_frontend *frontend,
                                            uint32_t guest_addr,
                                            uint8_t *guest_ptr, int flags,
                                            int *size, struct ir *ir);
typedef void (*jit_frontend_dump_code)(struct jit_frontend *frontend,
                                       uint32_t guest_addr, uint8_t *guest_ptr,
                                       int size);

struct jit_frontend {
  jit_frontend_translate_code translate_code;
  jit_frontend_dump_code dump_code;
};

#endif
