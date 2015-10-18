#ifndef TTY_WIN_H
#define TTY_WIN_H

#include "sys/tty.h"

namespace dreavm {
namespace sys {

class TTYWin : public TTY {
 public:
  bool Init();

  const char *Input();
  void Print(const char *buffer);
};
}
}

#endif
