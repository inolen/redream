#ifndef SH4_DISPATCH_H
#define SH4_DISPATCH_H

#include <stdint.h>

extern uint8_t sh4_code[];
extern int sh4_code_size;
extern int sh4_stack_size;

extern void *sh4_dispatch_dynamic;
extern void *sh4_dispatch_static;
extern void *sh4_dispatch_compile;
extern void *sh4_dispatch_interrupt;
extern void (*sh4_dispatch_enter)();
extern void *sh4_dispatch_leave;

void sh4_dispatch_init(void *sh4, void *jit, void *ctx, void *mem);
void *sh4_dispatch_lookup_code(uint32_t addr);
void sh4_dispatch_cache_code(uint32_t addr, void *code);
void sh4_dispatch_invalidate_code(uint32_t addr);
void sh4_dispatch_patch_edge(void *code, void *dst);
void sh4_dispatch_restore_edge(void *code, uint32_t dst);

#endif
