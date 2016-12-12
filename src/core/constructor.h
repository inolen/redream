#ifndef CONSTRUCTOR_H
#define CONSTRUCTOR_H

#if defined(__GNUC__)

#define CONSTRUCTOR(f)                              \
  static void f(void) __attribute__((constructor)); \
  static void f(void)

#define DESTRUCTOR(f)                              \
  static void f(void) __attribute__((destructor)); \
  static void f(void)

#elif defined(_MSC_VER)

#define CONSTRUCTOR(f)                                             \
  static void __cdecl f(void);                                     \
  __pragma(section(".CRT$XCU", read));                             \
  __declspec(allocate(".CRT$XCU")) void(__cdecl * f##_)(void) = f; \
  static void __cdecl f(void)

#define DESTRUCTOR(f)                                           \
  static void __cdecl f(void);                                  \
  static int _##f##_wrapper(void) {                             \
    atexit(f);                                                  \
    return 0;                                                   \
  }                                                             \
  __pragma(section(".CRT$XCU", read));                          \
  __declspec(allocate(".CRT$XCU")) void(__cdecl * f##_)(void) = \
      _##f##_wrapper;                                           \
  static void __cdecl f(void)

#else

/* not supported */

#endif

#endif
