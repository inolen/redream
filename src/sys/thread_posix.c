#include <pthread.h>
#include <stdlib.h>
#include "sys/thread.h"
#include "core/assert.h"

static void thread_destroy(pthread_t *pthread) {
  free(pthread);
}

thread_t thread_create(thread_fn fn, const char *name, void *data) {
  pthread_t *pthread = calloc(1, sizeof(pthread_t));

  if (pthread_create(pthread, NULL, fn, data)) {
    thread_destroy(pthread);
    return NULL;
  }

  return (thread_t)pthread;
}

void thread_join(thread_t thread, void **result) {
  pthread_t *pthread = (pthread_t *)thread;

  CHECK_EQ(pthread_join(*pthread, result), 0);

  thread_destroy(pthread);
}

mutex_t mutex_create() {
  pthread_mutex_t *pmutex = calloc(1, sizeof(pthread_mutex_t));

  pthread_mutex_init(pmutex, NULL);

  return (mutex_t)pmutex;
}

int mutex_trylock(mutex_t mutex) {
  pthread_mutex_t *pmutex = (pthread_mutex_t *)mutex;

  return pthread_mutex_trylock(pmutex) == 0;
}

void mutex_lock(mutex_t mutex) {
  pthread_mutex_t *pmutex = (pthread_mutex_t *)mutex;

  CHECK_EQ(pthread_mutex_lock(pmutex), 0);
}

void mutex_unlock(mutex_t mutex) {
  pthread_mutex_t *pmutex = (pthread_mutex_t *)mutex;

  CHECK_EQ(pthread_mutex_unlock(pmutex), 0);
}

void mutex_destroy(mutex_t mutex) {
  pthread_mutex_t *pmutex = (pthread_mutex_t *)mutex;

  pthread_mutex_destroy(pmutex);

  free(pmutex);
}