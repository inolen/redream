#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <userenv.h>
#include "sys/filesystem.h"

int fs_userdir(char *userdir, size_t size) {
  HANDLE accessToken = NULL;
  HANDLE processHandle = GetCurrentProcess();
  if (!OpenProcessToken(processHandle, TOKEN_QUERY, &accessToken)) {
    return 0;
  }

  if (!GetUserProfileDirectory(accessToken, (LPSTR)userdir, (LPDWORD)&size)) {
    CloseHandle(accessToken);
    return 0;
  }

  CloseHandle(accessToken);
  return 1;
}

int fs_exists(const char *path) {
  struct _stat buffer;
  return _stat(path, &buffer) == 0;
}

int fs_isdir(const char *path) {
  struct _stat buffer;
  if (_stat(path, &buffer) != 0) {
    return 0;
  }
  return (buffer.st_mode & S_IFDIR) == S_IFDIR;
}

int fs_isfile(const char *path) {
  struct _stat buffer;
  if (_stat(path, &buffer) != 0) {
    return 0;
  }
  return (buffer.st_mode & S_IFREG) == S_IFREG;
}

int fs_mkdir(const char *path) {
  int res = _mkdir(path);
  return res == 0 || errno == EEXIST;
}
