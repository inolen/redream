#ifndef FILES_H
#define FILES_H

#if PLATFORM_WINDOWS

#include <windows.h>
#include <direct.h>
#include <windows.h>
#include <dirent/dirent.h>

#define PATH_SEPARATOR "\\"
#define PATH_MAX MAX_PATH

#else

#include <dirent.h>
#include <limits.h>

#define PATH_SEPARATOR "/"

#endif

namespace re {
namespace sys {

bool GetUserDir(char *userdir, size_t size);
const char *GetAppDir();
void EnsureAppDirExists();

void DirName(const char *path, char *dir, size_t size);
void BaseName(const char *path, char *base, size_t size);

bool Exists(const char *path);
bool IsDir(const char *path);
bool IsFile(const char *path);

bool CreateDir(const char *path);
}
}

#endif
