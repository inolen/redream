#ifndef THREADS_H
#define THREADS_H

typedef void *thread_t;
typedef void *(*thread_fn)(void *);

thread_t thread_create(thread_fn fn, const char *name, void *data);
void thread_join(thread_t thread, void **result);

typedef void *mutex_t;

mutex_t mutex_create();
int mutex_trylock(mutex_t mutex);
void mutex_lock(mutex_t mutex);
void mutex_unlock(mutex_t mutex);
void mutex_destroy(mutex_t mutex);

#endif
