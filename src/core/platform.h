#ifndef DREAVM_PLATFORM_H
#define DREAVM_PLATFORM_H

#include <stdio.h>

#if defined(__linux)
#define PLATFORM_LINUX
#elif defined(__APPLE__)
#define PLATFORM_DARWIN
#elif defined(_WIN32) || defined(_WIN64)
#define PLATFORM_WINDOWS
#endif

#ifdef _MSC_VER

#include <direct.h>

#define getcwd _getcwd
#define snprintf _snprintf

#define PATH_SEPARATOR "\\"
#define PATH_MAX MAX_PATH

#else

#include <unistd.h>
#include <limits.h>

#define PATH_SEPARATOR "/"

#endif

#include <stddef.h>
#include <stdint.h>

void get_dirname(const char *path, char *dir, size_t size);
void get_basename(const char *path, char *base, size_t size);

#endif
