#include <errno.h>
#include <pwd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "sys/filesystem.h"

bool fs_userdir(char *userdir, size_t size) {
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

bool fs_exists(const char *path) {
  struct stat buffer;
  return stat(path, &buffer) == 0;
}

bool fs_isdir(const char *path) {
  struct stat buffer;
  if (stat(path, &buffer) != 0) {
    return false;
  }
  return (buffer.st_mode & S_IFDIR) == S_IFDIR;
}

bool fs_isfile(const char *path) {
  struct stat buffer;
  if (stat(path, &buffer) != 0) {
    return false;
  }
  return (buffer.st_mode & S_IFREG) == S_IFREG;
}

bool fs_mkdir(const char *path) {
  int res = mkdir(path, 0755);
  return res == 0 || errno == EEXIST;
}
