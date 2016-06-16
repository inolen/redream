#ifndef X64_BACKEND_H
#define X64_BACKEND_H

struct jit_backend_s;
struct mem_interface_s;

struct jit_backend_s *x64_backend_create(const struct mem_interface_s *memif);
void x64_backend_destroy(struct jit_backend_s *b);

#endif
