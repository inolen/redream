#include <algorithm>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include "core/core.h"
#include "sys/filesystem.h"

namespace dreavm {
namespace sys {

const char *GetAppDir() {
  static char appdir[PATH_MAX] = {};

  if (appdir[0]) {
    return appdir;
  }

  // get the user's home directory
  char userdir[PATH_MAX];
  if (!GetUserDir(userdir, sizeof(userdir))) {
    LOG_FATAL("Failed to locate user directory");
  }

  // setup our own subdirectory inside of it
  snprintf(appdir, sizeof(appdir), "%s" PATH_SEPARATOR ".dreavm", userdir);

  return appdir;
}

void EnsureAppDirExists() {
  const char *appdir = GetAppDir();

  if (!CreateDir(appdir)) {
    LOG_FATAL("Failed to create app directory %s", appdir);
  }
}

void DirName(const char *path, char *dir, size_t size) {
  if (!path || !*path) {
    strncpy(dir, ".", size);
    return;
  }
  size_t i = strlen(path) - 1;
  for (; path[i] == PATH_SEPARATOR[0]; i--) {
    if (!i) {
      strncpy(dir, PATH_SEPARATOR, size);
      return;
    }
  }
  for (; path[i] != PATH_SEPARATOR[0]; i--) {
    if (!i) {
      strncpy(dir, ".", size);
      return;
    }
  }
  for (; path[i] == PATH_SEPARATOR[0]; i--) {
    if (!i) {
      strncpy(dir, PATH_SEPARATOR, size);
      return;
    }
  }
  size_t n = std::min(i + 1, size - 1);
  strncpy(dir, path, n);
  dir[n] = 0;
}

void BaseName(const char *path, char *base, size_t size) {
  if (!path || !*path) {
    strncpy(base, ".", size);
    return;
  }
  size_t len = strlen(path);
  size_t i = len - 1;
  for (; i && path[i] == PATH_SEPARATOR[0]; i--) {
    len = i;
  }
  for (; i && path[i - 1] != PATH_SEPARATOR[0]; i--) {}
  size_t n = std::min(len - i, size - 1);
  strncpy(base, path + i, n);
  base[n] = 0;
}
}
}
