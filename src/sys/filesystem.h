#ifndef FILES_H
#define FILES_H

#include <stdbool.h>
#include <stdio.h>

#if PLATFORM_LINUX || PLATFORM_DARWIN

#if PLATFORM_LINUX
#include <linux/limits.h>
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
#define PATH_MAX MAX_PATH

#endif

bool fs_userdir(char *userdir, size_t size);
const char *fs_appdir();

void fs_dirname(const char *path, char *dir, size_t size);
void fs_basename(const char *path, char *base, size_t size);

bool fs_exists(const char *path);
bool fs_isdir(const char *path);
bool fs_isfile(const char *path);
bool fs_mkdir(const char *path);

#endif
