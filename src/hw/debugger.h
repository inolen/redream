#ifndef DEBUGGER_H
#define DEBUGGER_H

#include <stdint.h>
#include "gdb/gdb_server.h"

namespace re {
namespace hw {

class Machine;
class DebugInterface;

class Debugger {
 public:
  Debugger(Machine &machine);
  ~Debugger();

  bool Init();
  void Trap();
  void PumpEvents();

 private:
  static void gdb_server_stop(void *data);
  static void gdb_server_resume(void *data);
  static void gdb_server_step(void *data);
  static void gdb_server_add_bp(void *data, int type, intmax_t addr);
  static void gdb_server_rem_bp(void *data, int type, intmax_t addr);
  static void gdb_server_read_mem(void *data, intmax_t addr, uint8_t *buffer,
                                  int size);
  static void gdb_server_read_reg(void *data, int n, intmax_t *value,
                                  int *size);

  Machine &machine_;
  DebugInterface *debug_;
  gdb_server_t *sv_;
};
}
}

#endif
