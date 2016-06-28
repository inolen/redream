#include <pthread.h>
#include <stdlib.h>
#include "sys/thread.h"

static void thread_destroy(thread_t thread) {
  pthread_t *pthread = (pthread_t *)thread;

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

void thread_detach(thread_t thread) {
  pthread_t *pthread = (pthread_t *)thread;

  pthread_detach(*pthread);
}

void thread_join(thread_t thread, void **result) {
  pthread_t *pthread = (pthread_t *)thread;

  pthread_join(*pthread, result);
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

  pthread_mutex_lock(pmutex);
}

void mutex_unlock(mutex_t mutex) {
  pthread_mutex_t *pmutex = (pthread_mutex_t *)mutex;

  pthread_mutex_unlock(pmutex);
}

void mutex_destroy(mutex_t mutex) {
  pthread_mutex_t *pmutex = (pthread_mutex_t *)mutex;

  pthread_mutex_destroy(pmutex);

  free(pmutex);
}

cond_t cond_create() {
  pthread_cond_t *pcond = calloc(1, sizeof(pthread_mutex_t));

  pthread_cond_init(pcond, NULL);

  return (cond_t)pcond;
}

void cond_wait(cond_t cond, mutex_t mutex) {
  pthread_cond_t *pcond = (pthread_cond_t *)cond;
  pthread_mutex_t *pmutex = (pthread_mutex_t *)mutex;

  pthread_cond_wait(pcond, pmutex);
}

void cond_signal(cond_t cond) {
  pthread_cond_t *pcond = (pthread_cond_t *)cond;

  pthread_cond_signal(pcond);
}

void cond_destroy(cond_t cond) {
  pthread_cond_t *pcond = (pthread_cond_t *)cond;

  pthread_cond_destroy(pcond);

  free(pcond);
}
