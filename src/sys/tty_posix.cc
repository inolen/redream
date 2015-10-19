#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "core/core.h"
#include "sys/tty_posix.h"

static const char *tty_prompt = "[dreavm] ";

using namespace dreavm;
using namespace dreavm::sys;

TTY &TTY::instance() {
  static TTYPosix instance;
  return instance;
}

TTYPosix::TTYPosix() : front_(0), cursor_(0), buffer_() {}

TTYPosix::~TTYPosix() {
  // restore original terminal parameters
  tcsetattr(STDIN_FILENO, TCSADRAIN, &old_tc_);

  // restore blocking stdin
  int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
  fcntl(STDIN_FILENO, F_SETFL, flags & ~O_NONBLOCK);
}

bool TTYPosix::Init() {
  // set stdin to be nonblocking
  int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
  fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);

  // save off original terminal parameters
  tcgetattr(STDIN_FILENO, &old_tc_);

  // setup new parameters
  termios tc = old_tc_;

  // disable input echoing and canonical mode. disabling canonical mode
  // enables the reading of individual characters before the EOL
  tc.c_lflag &= ~(ECHO | ICANON);

  // disable parity bit being set on input
  tc.c_iflag &= ~(ISTRIP | INPCK);

  // set read to complete immediately upon a single character being input
  tc.c_cc[VMIN] = 1;
  tc.c_cc[VTIME] = 0;

  // update parameters
  tcsetattr(STDIN_FILENO, TCSADRAIN, &tc);

  return true;
}

const char *TTYPosix::Input() {
  char key = 0;
  int r = 0;

  while (true) {
    r = read(STDIN_FILENO, &key, 1);

    if (r != 1) {
      break;
    }

    // erase last character
    if (key == '\b' || key == (char)127 /* delete */) {
      if (cursor_ > 0) {
        buffer_[front_][--cursor_] = 0;
        Back();
      }
      continue;
    }

    // terminate processing on newline
    if (key == '\n') {
      // clear existing prompt
      HidePrompt();

      // flip input buffer
      const char *ret = buffer_[front_];
      front_ = !front_;
      cursor_ = 0;
      buffer_[front_][cursor_] = 0;

      // show new blank prompt
      ShowPrompt();

      // return old buffer
      return ret;
    }

    // input full
    if (cursor_ >= (int)sizeof(buffer_[0]) - 1) {
      return nullptr;
    }

    // append new character to buffer
    buffer_[front_][cursor_++] = key;
    buffer_[front_][cursor_] = 0;

    // write out new character
    write(STDOUT_FILENO, &key, 1);
  }

  return nullptr;
}

void TTYPosix::Print(const char *buffer) {
  HidePrompt();
  puts(buffer);
  ShowPrompt();
}

void TTYPosix::Back() {
  // backspace will just move the cursor left, output backspace-space-backspace
  // to visually erase it
  char key = '\b';
  write(STDOUT_FILENO, &key, 1);
  key = ' ';
  write(STDOUT_FILENO, &key, 1);
  key = '\b';
  write(STDOUT_FILENO, &key, 1);
}

void TTYPosix::HidePrompt() {
  for (int i = 0; i < cursor_; i++) {
    Back();
  }

  for (int i = 0; i < (int)strlen(tty_prompt); i++) {
    Back();
  }
}

void TTYPosix::ShowPrompt() {
  write(STDOUT_FILENO, tty_prompt, strlen(tty_prompt));

  for (int i = 0; i < cursor_; i++) {
    write(STDOUT_FILENO, &buffer_[front_][i], 1);
  }
}
