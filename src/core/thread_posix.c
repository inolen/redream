#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <time.h>
#include "core/core.h"
#include "core/thread.h"

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

  int res = pthread_mutex_init(pmutex, NULL);
  CHECK_EQ(res, 0);

  return (mutex_t)pmutex;
}

int mutex_trylock(mutex_t mutex) {
  pthread_mutex_t *pmutex = (pthread_mutex_t *)mutex;

  int res = pthread_mutex_trylock(pmutex);
  if (res == 0) {
    return 1;
  }

  CHECK_EQ(res, EBUSY);
  return 0;
}

void mutex_lock(mutex_t mutex) {
  pthread_mutex_t *pmutex = (pthread_mutex_t *)mutex;

  int res = pthread_mutex_lock(pmutex);
  CHECK_EQ(res, 0);
}

void mutex_unlock(mutex_t mutex) {
  pthread_mutex_t *pmutex = (pthread_mutex_t *)mutex;

  int res = pthread_mutex_unlock(pmutex);
  CHECK_EQ(res, 0);
}

void mutex_destroy(mutex_t mutex) {
  pthread_mutex_t *pmutex = (pthread_mutex_t *)mutex;

  int res = pthread_mutex_destroy(pmutex);
  CHECK_EQ(res, 0);

  free(pmutex);
}

cond_t cond_create() {
  pthread_cond_t *pcond = calloc(1, sizeof(pthread_cond_t));

  int res = pthread_cond_init(pcond, NULL);
  CHECK_EQ(res, 0);

  return (cond_t)pcond;
}

void cond_wait(cond_t cond, mutex_t mutex) {
  pthread_cond_t *pcond = (pthread_cond_t *)cond;
  pthread_mutex_t *pmutex = (pthread_mutex_t *)mutex;

  int res = pthread_cond_wait(pcond, pmutex);
  CHECK_EQ(res, 0);
}

int cond_timedwait(cond_t cond, mutex_t mutex, int ms) {
  pthread_cond_t *pcond = (pthread_cond_t *)cond;
  pthread_mutex_t *pmutex = (pthread_mutex_t *)mutex;

  struct timespec wait;
  clock_gettime(CLOCK_REALTIME, &wait);
  wait.tv_sec += ms / 1000;
  wait.tv_nsec += ms % 1000;

  int res = pthread_cond_timedwait(pcond, pmutex, &wait);
  if (res == 0) {
    return 1;
  }

  CHECK_EQ(res, ETIMEDOUT);
  return 0;
}

void cond_signal(cond_t cond) {
  pthread_cond_t *pcond = (pthread_cond_t *)cond;

  int res = pthread_cond_signal(cond);
  CHECK_EQ(res, 0);
}

void cond_destroy(cond_t cond) {
  pthread_cond_t *pcond = (pthread_cond_t *)cond;

  int res = pthread_cond_destroy(pcond);
  CHECK_EQ(res, 0);

  free(pcond);
}
