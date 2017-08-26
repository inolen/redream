#include <errno.h>
#include "core/filesystem.h"
#include "core/log.h"
#include "core/string.h"

static char appdir[PATH_MAX];

static inline int fs_is_separator(char c) {
#if PLATFORM_WINDOWS
  /* it's not sufficient to compare against the platform-specific PATH_SEPARATOR
     on Windows as paths may contain both separators, particularly when using
     one of the Unix-like shell environments (e.g. msys or cygwin) */
  return c == '\\' || c == '/';
#else
  return c == PATH_SEPARATOR[0];
#endif
}

void fs_basename(const char *path, char *base, size_t size) {
  if (!path || !*path) {
    strncpy(base, ".", size);
    return;
  }
  size_t len = strlen(path);
  size_t i = len - 1;
  for (; i && fs_is_separator(path[i]); i--) {
    len = i;
  }
  for (; i && !fs_is_separator(path[i - 1]); i--) {
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
  for (; fs_is_separator(path[i]); i--) {
    if (!i) {
      strncpy(dir, PATH_SEPARATOR, size);
      return;
    }
  }
  for (; !fs_is_separator(path[i]); i--) {
    if (!i) {
      strncpy(dir, ".", size);
      return;
    }
  }
  for (; fs_is_separator(path[i]); i--) {
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
