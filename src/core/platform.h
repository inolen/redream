#ifndef DREAVM_PLATFORM_H
#define DREAVM_PLATFORM_H

#if defined(__linux)
#define PLATFORM_LINUX
#elif defined(__APPLE__)
#define PLATFORM_DARWIN
#elif defined(_WIN32) || defined(_WIN64)
#define PLATFORM_WINDOWS
#endif

#endif
