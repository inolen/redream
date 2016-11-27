#ifndef ARM7_DISPATCH_H
#define ARM7_DISPATCH_H

#include <stdint.h>

extern uint8_t arm7_code[];
extern int arm7_code_size;
extern int arm7_stack_size;

extern void *arm7_dispatch_dynamic;
extern void *arm7_dispatch_static;
extern void *arm7_dispatch_compile;
extern void *arm7_dispatch_interrupt;
extern void (*arm7_dispatch_enter)();
extern void *arm7_dispatch_leave;

void arm7_dispatch_init(void *sh4, void *jit, void *ctx, void *mem);
void *arm7_dispatch_lookup_code(uint32_t addr);
void arm7_dispatch_cache_code(uint32_t addr, void *code);
void arm7_dispatch_invalidate_code(uint32_t addr);
void arm7_dispatch_patch_edge(void *code, void *dst);
void arm7_dispatch_restore_edge(void *code, uint32_t dst);

#endif
