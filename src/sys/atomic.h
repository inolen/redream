#ifndef SYS_ATOMIC_H
#define SYS_ATOMIC_H

#ifdef __cplusplus

#include <atomic>

struct re_atomic_int {
    std::atomic<int> x;
};

struct re_atomic_long {
    std::atomic<long> x;
};

#define ATOMIC_LOAD(a) (a.x.load())
#define ATOMIC_FETCH_ADD(a, value) (a.x.fetch_add(value))
#define ATOMIC_STORE(a, value) (a.x.store(value))
#define ATOMIC_EXCHANGE(a, value) (a.x.exchange(value))

#else

#include <stdatomic.h>

struct re_atomic_int {
    atomic_int x;
};

struct re_atomic_long {
    atomic_long x;
};

#define ATOMIC_LOAD(a) atomic_load(&a.x)
#define ATOMIC_FETCH_ADD(a, value) atomic_fetch_add(&a.x, value)
#define ATOMIC_STORE(a, value) atomic_store(&a.x, value)
#define ATOMIC_EXCHANGE(a, value) atomic_exchange(&a.x, value)

#endif

#endif
