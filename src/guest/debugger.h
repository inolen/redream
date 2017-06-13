#ifndef DEBUGGER_H
#define DEBUGGER_H

#include <stdint.h>

struct dreamcast;
struct debugger;

struct debugger *debugger_create(struct dreamcast *dc);
void debugger_destroy(struct debugger *dbg);

int debugger_init(struct debugger *dbg);
void debugger_trap(struct debugger *dbg);
void debugger_tick(struct debugger *dbg);

#endif
