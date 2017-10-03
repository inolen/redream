#include <stdlib.h>
#include <windows.h>
#include "core/core.h"
#include "core/thread.h"

struct thread_wrapper {
  thread_fn fn;
  void *data;
  HANDLE handle;
};

static DWORD thread_thunk(LPVOID data) {
  struct thread_wrapper *wrapper = data;

  return (DWORD)(intptr_t)wrapper->fn(wrapper->data);
}

static void thread_destroy(struct thread_wrapper *wrapper) {
  free(wrapper);
}

thread_t thread_create(thread_fn fn, const char *name, void *data) {
  struct thread_wrapper *wrapper = calloc(1, sizeof(struct thread_wrapper));

  wrapper->fn = fn;
  wrapper->data = data;
  wrapper->handle = CreateThread(NULL, 0, &thread_thunk, wrapper, 0, NULL);

  if (!wrapper->handle) {
    thread_destroy(wrapper);
    return NULL;
  }

  return (thread_t)wrapper;
}

void thread_join(thread_t thread, void **result) {
  struct thread_wrapper *wrapper = (struct thread_wrapper *)thread;

  CHECK_EQ(WaitForSingleObject(wrapper->handle, INFINITE), WAIT_OBJECT_0);

  thread_destroy(wrapper);
}

mutex_t mutex_create() {
  CRITICAL_SECTION *wmutex = calloc(1, sizeof(CRITICAL_SECTION));

  InitializeCriticalSection(wmutex);

  return (mutex_t)wmutex;
}

int mutex_trylock(mutex_t mutex) {
  CRITICAL_SECTION *wmutex = (CRITICAL_SECTION *)mutex;

  BOOL res = TryEnterCriticalSection(wmutex);
  return res;
}

void mutex_lock(mutex_t mutex) {
  CRITICAL_SECTION *wmutex = (CRITICAL_SECTION *)mutex;

  EnterCriticalSection(wmutex);
}

void mutex_unlock(mutex_t mutex) {
  CRITICAL_SECTION *wmutex = (CRITICAL_SECTION *)mutex;

  LeaveCriticalSection(wmutex);
}

void mutex_destroy(mutex_t mutex) {
  CRITICAL_SECTION *wmutex = (CRITICAL_SECTION *)mutex;

  DeleteCriticalSection(wmutex);
}

cond_t cond_create() {
  CONDITION_VARIABLE *wcond = calloc(1, sizeof(CONDITION_VARIABLE));

  InitializeConditionVariable(wcond);

  return (cond_t)wcond;
}

void cond_wait(cond_t cond, mutex_t mutex) {
  CONDITION_VARIABLE *wcond = (CONDITION_VARIABLE *)cond;
  CRITICAL_SECTION *wmutex = (CRITICAL_SECTION *)mutex;

  BOOL res = SleepConditionVariableCS(wcond, wmutex, INFINITE);
  CHECK_NE(res, 0);
}

int cond_timedwait(cond_t cond, mutex_t mutex, int ms) {
  CONDITION_VARIABLE *wcond = (CONDITION_VARIABLE *)cond;
  CRITICAL_SECTION *wmutex = (CRITICAL_SECTION *)mutex;

  BOOL res = SleepConditionVariableCS(wcond, wmutex, ms);
  return res;
}

void cond_signal(cond_t cond) {
  CONDITION_VARIABLE *wcond = (CONDITION_VARIABLE *)cond;

  WakeConditionVariable(wcond);
}

void cond_destroy(cond_t cond) {
  CONDITION_VARIABLE *wcond = (CONDITION_VARIABLE *)cond;

  free(wcond);
}
