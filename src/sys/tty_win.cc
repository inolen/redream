#include "sys/tty_win.h"
using namespace dreavm::sys;

TTY &TTY::instance() {
  static TTYWin instance;
  return instance;
}

bool TTYPosix::Init() { return true; }

const char *TTYPosix::Input() { return nullptr; }

void TTYPosix::Print(const char *buffer) { puts(buffer); }
