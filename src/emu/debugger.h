#ifndef DEBUGGER_H
#define DEBUGGER_H

namespace dvm {
namespace emu {

class Debugger {
 public:
  void HandleCommand(const char *cmd);
};
}
}

#endif
