#ifndef X64_BACKEND_H
#define X64_BACKEND_H

struct jit_backend;
struct mem_interface;

struct jit_backend *x64_backend_create(const struct mem_interface *memif);
void x64_backend_destroy(struct jit_backend *b);

#endif
