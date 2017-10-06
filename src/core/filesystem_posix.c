#include <errno.h>
#include <pwd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "core/core.h"
#include "core/filesystem.h"

int fs_mkdir(const char *path) {
  int res = mkdir(path, 0755);
  return res == 0 || errno == EEXIST;
}

int fs_isfile(const char *path) {
  struct stat buffer;
  if (stat(path, &buffer) != 0) {
    return 0;
  }
  return (buffer.st_mode & S_IFREG) == S_IFREG;
}

int fs_isdir(const char *path) {
  struct stat buffer;
  if (stat(path, &buffer) != 0) {
    return 0;
  }
  return (buffer.st_mode & S_IFDIR) == S_IFDIR;
}

int fs_exists(const char *path) {
  struct stat buffer;
  return stat(path, &buffer) == 0;
}

void fs_realpath(const char *path, char *resolved, size_t size) {
  char tmp[PATH_MAX];
  if (realpath(path, tmp)) {
    strncpy(resolved, tmp, size);
  } else {
    strncpy(resolved, path, size);
  }
}

int fs_mediadirs(char *dirs, int num, size_t size) {
  char *ptr = dirs;
  char *end = dirs + size * num;

  /* search in the home directory */
  static const char *home_search[] = {"Desktop", "Documents", "Downloads",
                                      "Music",   "Pictures",  "Videos"};

  char home[PATH_MAX];
  int res = fs_userdir(home, sizeof(home));

  if (res) {
    for (int i = 0; i < ARRAY_SIZE(home_search); i++) {
      char path[PATH_MAX];
      snprintf(path, sizeof(path), "%s" PATH_SEPARATOR "%s", home,
               home_search[i]);

      if (!fs_isdir(path)) {
        continue;
      }

      if (ptr < end) {
        strncpy(ptr, path, size);
        ptr += size;
      }
    }
  }

  /* search for additional mounts */
  const char *mnt_search[] = {
#if PLATFORM_DARWIN
    "/Volumes",
#else
    "/media",
    "/mnt"
#endif
  };

  for (int i = 0; i < ARRAY_SIZE(mnt_search); i++) {
    const char *path = mnt_search[i];

    DIR *dir = opendir(path);
    if (!dir) {
      continue;
    }

    struct dirent *ent = NULL;
    while ((ent = readdir(dir)) != NULL) {
      const char *dname = ent->d_name;

      /* ignore special directories */
      if (!strcmp(dname, "..") || !strcmp(dname, ".")) {
        continue;
      }

      if (ptr < end) {
        snprintf(ptr, size, "%s" PATH_SEPARATOR "%s", path, dname);
        ptr += size;
      }
    }

    closedir(dir);
  }

  return (ptr - dirs) / size;
}

int fs_userdir(char *userdir, size_t size) {
  const char *home = getenv("HOME");

  if (home) {
    strncpy(userdir, home, size);
    return 1;
  }

  struct passwd *pw = getpwuid(getuid());
  if (pw->pw_dir) {
    strncpy(userdir, pw->pw_dir, size);
    return 1;
  }

  return 0;
}
