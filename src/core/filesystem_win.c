#include <Windows.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <userenv.h>
#include "core/filesystem.h"

int fs_mkdir(const char *path) {
  int res = _mkdir(path);
  return res == 0 || errno == EEXIST;
}

int fs_isfile(const char *path) {
  struct _stat buffer;
  if (_stat(path, &buffer) != 0) {
    return 0;
  }
  return (buffer.st_mode & S_IFREG) == S_IFREG;
}

int fs_isdir(const char *path) {
  struct _stat buffer;
  if (_stat(path, &buffer) != 0) {
    return 0;
  }
  return (buffer.st_mode & S_IFDIR) == S_IFDIR;
}

int fs_exists(const char *path) {
  struct _stat buffer;
  return _stat(path, &buffer) == 0;
}

void fs_realpath(const char *path, char *resolved, size_t size) {
  if (!_fullpath(resolved, path, size)) {
    strncpy(resolved, path, size);
  }
}

int fs_mediadirs(char *dirs, int num, size_t size) {
  char *ptr = dirs;
  char *end = dirs + size * num;
  char path[PATH_MAX];

  DWORD drives = GetLogicalDrives();
  int max_drives = (int)sizeof(drives) * 8;

  for (int i = 0; i < max_drives; i++) {
    if (!(drives & (1 << i))) {
      continue;
    }

    if (ptr < end) {
      snprintf(path, sizeof(path), "%c:" PATH_SEPARATOR, 'A' + i);
      strncpy(ptr, path, size);
      ptr += size;
    }
  }

  return (int)((ptr - dirs) / size);
}

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
