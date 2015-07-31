#ifndef DREAVM_PLATFORM_H
#define DREAVM_PLATFORM_H

#if defined(__linux)
#define PLATFORM_LINUX
#elif defined(__APPLE__)
#define PLATFORM_DARWIN
#elif defined(_WIN32) || defined(_WIN64)
#define PLATFORM_WINDOWS
#endif

#ifdef PLATFORM_WINDOWS

#include <direct.h>

#define PATH_SEPARATOR "\\"
#define PATH_MAX MAX_PATH

#else

#include <limits.h>

#define PATH_SEPARATOR "/"

#endif

namespace dreavm {
namespace core {

bool getuserdir(char *userdir, size_t size);

void dirname(const char *path, char *dir, size_t size);
void basename(const char *path, char *base, size_t size);

bool exists(const char *path);
bool mkdir(const char *path);
}
}

#endif
