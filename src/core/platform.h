#ifndef DREAVM_PLATFORM_H
#define DREAVM_PLATFORM_H

#if defined(__linux)
#define PLATFORM_LINUX
#elif defined(__APPLE__)
#define PLATFORM_DARWIN
#elif defined(_WIN32) || defined(_WIN64)
#define PLATFORM_WINDOWS
#else
#error "Unsupported platform"
#endif

#if defined(__GNUC__)
#define COMPILER_GCC
#elif defined(__clang__)
#define COMPILER_CLANG
#elif defined(_MSC_VER)
#define COMPILER_MSVC
#else
#error "Unsupported compiler"
#endif

#endif
