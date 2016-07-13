#include <stdlib.h>
#include <windows.h>
#include "sys/thread.h"
#include "core/assert.h"

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
  HANDLE wmutex = CreateMutex(NULL, FALSE, NULL);

  return (mutex_t)wmutex;
}

int mutex_trylock(mutex_t mutex) {
  HANDLE wmutex = (HANDLE)mutex;

  return WaitForSingleObject(wmutex, 0) == WAIT_OBJECT_0;
}

void mutex_lock(mutex_t mutex) {
  HANDLE wmutex = (HANDLE)mutex;

  CHECK_EQ(WaitForSingleObject(wmutex, INFINITE), WAIT_OBJECT_0);
}

void mutex_unlock(mutex_t mutex) {
  HANDLE wmutex = (HANDLE)mutex;

  CHECK_NE(ReleaseMutex(wmutex), 0);
}

void mutex_destroy(mutex_t mutex) {
  HANDLE wmutex = (HANDLE)mutex;

  CloseHandle(wmutex);
}