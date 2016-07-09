#include <errno.h>
#include <stdlib.h>
#include "sys/filesystem.h"
#include "core/debug_break.h"
#include "core/log.h"
#include "core/math.h"
#include "core/string.h"

const char *fs_appdir() {
  static char appdir[PATH_MAX];

  if (appdir[0]) {
    return appdir;
  }

  // get the user's home directory
  char userdir[PATH_MAX];
  if (!fs_userdir(userdir, sizeof(userdir))) {
    LOG_FATAL("Failed to locate user directory");
  }

  // setup our own subdirectory inside of it
  snprintf(appdir, sizeof(appdir), "%s" PATH_SEPARATOR ".redream", userdir);

  return appdir;
}

void fs_dirname(const char *path, char *dir, size_t size) {
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
  size_t n = MIN(i + 1, size - 1);
  strncpy(dir, path, n);
  dir[n] = 0;
}

void fs_basename(const char *path, char *base, size_t size) {
  if (!path || !*path) {
    strncpy(base, ".", size);
    return;
  }
  size_t len = strlen(path);
  size_t i = len - 1;
  for (; i && path[i] == PATH_SEPARATOR[0]; i--) {
    len = i;
  }
  for (; i && path[i - 1] != PATH_SEPARATOR[0]; i--) {
  }
  size_t n = MIN(len - i, size - 1);
  strncpy(base, path + i, n);
  base[n] = 0;
}
