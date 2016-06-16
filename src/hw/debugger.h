#ifndef DEBUGGER_H
#define DEBUGGER_H

#include <stdbool.h>
#include <stdint.h>

struct dreamcast_s;
struct debugger_s;

bool debugger_init(struct debugger_s *dbg);
void debugger_trap(struct debugger_s *dbg);
void debugger_tick(struct debugger_s *dbg);

struct debugger_s *debugger_create(struct dreamcast_s *dc);
void debugger_destroy(struct debugger_s *dbg);

#endif
