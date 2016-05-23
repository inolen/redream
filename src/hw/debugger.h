#ifndef DEBUGGER_H
#define DEBUGGER_H

#include <stdbool.h>
#include <stdint.h>
#include "gdb/gdb_server.h"

#ifdef __cplusplus
extern "C" {
#endif

struct dreamcast_s;
struct debugger_s;

struct debugger_s *debugger_create(struct dreamcast_s *dc);
void debugger_destroy(struct debugger_s *dbg);
bool debugger_init(struct debugger_s *dbg);
void debugger_trap(struct debugger_s *dbg);
void debugger_tick(struct debugger_s *dbg);

#ifdef __cplusplus
}
#endif

#endif
