#ifndef FILES_H
#define FILES_H

#include <stdio.h>

#if PLATFORM_LINUX || PLATFORM_DARWIN

#if PLATFORM_LINUX
#include <limits.h>
#else
#include <sys/syslimits.h>
#endif
#include <dirent.h>

#define PATH_SEPARATOR "/"

#else

#include <direct.h>
#include <dirent/dirent.h>
#include <windows.h>

#define PATH_SEPARATOR "\\"

/* careful, MinGW may have defined this */
#ifndef PATH_MAX
#define PATH_MAX MAX_PATH
#endif

#endif

const char *fs_appdir();
void fs_set_appdir(const char *path);

int fs_userdir(char *userdir, size_t size);

void fs_dirname(const char *path, char *dir, size_t size);
void fs_basename(const char *path, char *base, size_t size);

int fs_exists(const char *path);
int fs_isdir(const char *path);
int fs_isfile(const char *path);
int fs_mkdir(const char *path);

#endif
