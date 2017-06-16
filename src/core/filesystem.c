#include <errno.h>
#include "core/filesystem.h"
#include "core/log.h"
#include "core/string.h"

static char appdir[PATH_MAX];

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

void fs_set_appdir(const char *path) {
  strncpy(appdir, path, sizeof(appdir));

  if (!fs_mkdir(appdir)) {
    LOG_FATAL("fs_set_appdir failed to create app directory %s", appdir);
  }
}

const char *fs_appdir() {
  return appdir;
}
