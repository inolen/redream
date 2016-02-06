#include <algorithm>
#include <errno.h>
#include <pwd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "sys/filesystem.h"

namespace re {
namespace sys {

bool GetUserDir(char *userdir, size_t size) {
  const char *home = getenv("HOME");

  if (home) {
    strncpy(userdir, home, size);
    return true;
  }

  struct passwd *pw = getpwuid(getuid());
  if (pw->pw_dir) {
    strncpy(userdir, pw->pw_dir, size);
    return true;
  }

  return false;
}

bool Exists(const char *path) {
  struct stat buffer;
  return stat(path, &buffer) == 0;
}

bool CreateDir(const char *path) {
  int res = mkdir(path, 0755);
  return res == 0 || errno == EEXIST;
}
}
}
