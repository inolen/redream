#ifndef DEBUGGER_H
#define DEBUGGER_H

namespace dreavm {
namespace emu {

class Debugger {
 public:
  void HandleCommand(const char *cmd);
};
}
}

#endif
