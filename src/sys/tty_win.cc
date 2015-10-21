#include <stdio.h>
#include "sys/tty_win.h"
using namespace dreavm::sys;

TTY &TTY::instance() {
  static TTYWin instance;
  return instance;
}

bool TTYWin::Init() { return true; }

const char *TTYWin::Input() { return nullptr; }

void TTYWin::Print(const char *buffer) { puts(buffer); }
