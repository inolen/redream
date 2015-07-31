#include <algorithm>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#ifdef PLATFORM_WINDOWS
#include <userenv.h>
#else
#include <pwd.h>
#endif

#include "core/platform.h"

namespace dreavm {
namespace core {

bool getuserdir(char *userdir, size_t size) {
#ifdef PLATFORM_WINDOWS
  HANDLE accessToken = NULL;
  HANDLE processHandle = GetCurrentProcess();
  if (!OpenProcessToken(processHandle, TOKEN_QUERY, &accessToken)) {
    return false;
  }

  if (!GetUserProfileDirectory(accessToken, (LPSTR)userdir, (LPDWORD)&size)) {
    CloseHandle(accessToken);
    return false;
  }

  CloseHandle(accessToken);
  return true;
#else
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
#endif
}

void dirname(const char *path, char *dir, size_t size) {
  if (!path || !*path) {
    strncpy(dir, ".", size);
    return;
  }
  size_t i = strlen(path) - 1;
  for (; path[i] == PATH_SEPARATOR[0]; i--)
    if (!i) {
      strncpy(dir, PATH_SEPARATOR, size);
      return;
    }
  for (; path[i] != PATH_SEPARATOR[0]; i--)
    if (!i) {
      strncpy(dir, ".", size);
      return;
    }
  for (; path[i] == PATH_SEPARATOR[0]; i--)
    if (!i) {
      strncpy(dir, PATH_SEPARATOR, size);
      return;
    }
  size_t n = std::min(i + 1, size - 1);
  strncpy(dir, path, n);
  dir[n] = 0;
}

void basename(const char *path, char *base, size_t size) {
  if (!path || !*path) {
    strncpy(base, ".", size);
    return;
  }
  size_t len = strlen(path);
  size_t i = len - 1;
  for (; i && path[i] == PATH_SEPARATOR[0]; i--) len = i;
  for (; i && path[i - 1] != PATH_SEPARATOR[0]; i--)
    ;
  size_t n = std::min(len - i, size - 1);
  strncpy(base, path + i, n);
  base[n] = 0;
}

bool exists(const char *path) {
#ifdef PLATFORM_WINDOWS
  struct _stat buffer;
  return _stat(path, &buffer) == 0;
#else
  struct stat buffer;
  return stat(path, &buffer) == 0;
#endif
}

bool mkdir(const char *path) {
#ifdef PLATFORM_WINDOWS
  int res = ::_mkdir(path);
#else
  int res = ::mkdir(path, 0755);
#endif
  return res == 0 || errno == EEXIST;
}
}
}
