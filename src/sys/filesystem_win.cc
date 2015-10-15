#include <algorithm>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <userenv.h>
#include "core/core.h"
#include "sys/filesystem.h"

namespace dreavm {
namespace sys {

bool GetUserDir(char *userdir, size_t size) {
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
}

bool Exists(const char *path) {
  struct _stat buffer;
  return _stat(path, &buffer) == 0;
}

bool CreateDir(const char *path) {
  int res = _mkdir(path);
  return res == 0 || errno == EEXIST;
}
}
}
