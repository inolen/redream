#ifndef TTY_POSIX_H
#define TTY_POSIX_H

#include <termios.h>
#include "sys/tty.h"

namespace dreavm {
namespace sys {

enum { TTY_BUFFER_SIZE = 1024 };

class TTYPosix : public TTY {
 public:
  TTYPosix();
  ~TTYPosix();

  bool Init();

  const char *Input();
  void Print(const char *buffer);

 private:
  void Back();
  void HidePrompt();
  void ShowPrompt();

  termios old_tc_;
  int front_;
  int cursor_;
  char buffer_[2][TTY_BUFFER_SIZE];
};
}
}

#endif
