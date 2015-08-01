#ifndef FILESYSTEM_H
#define FILESYSTEM_H

#include "core/platform.h"

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

const char *GetAppDir();
void EnsureAppDirExists();

void DirName(const char *path, char *dir, size_t size);
void BaseName(const char *path, char *base, size_t size);

bool Exists(const char *path);
bool CreateDir(const char *path);
}
}

#endif
