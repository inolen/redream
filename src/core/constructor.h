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
  __declspec(allocate(".CRT$XCU")) void(__cdecl * f##_)(void) = f; \
  static void __cdecl f(void)

#define DESTRUCTOR(f)                                                         \
  static void __cdecl f(void);                                                \
  static int _f##_wrapper(void) {                                             \
    atexit(f);                                                                \
    return 0;                                                                 \
  }                                                                           \
  __declspec(allocate(".CRT$XCU")) void(__cdecl * f##_)(void) = _f##_wrapper; \
  static void __cdecl f(void)

#else

// not supported

#endif

#endif
