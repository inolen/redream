#ifndef SYS_MEMORY_H
#define SYS_MEMORY_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

struct re_exception_s;

//
// page protection
//
typedef enum {
  ACC_NONE,
  ACC_READONLY,
  ACC_READWRITE,
  ACC_READWRITEEXEC,
} page_access_t;

size_t get_page_size();
size_t get_allocation_granularity();
bool protect_pages(void *ptr, size_t size, page_access_t access);
bool reserve_pages(void *ptr, size_t size);
bool release_pages(void *ptr, size_t size);

//
// shared memory objects
//
typedef void *shmem_handle_t;
#define SHMEM_INVALID NULL

shmem_handle_t create_shared_memory(const char *filename, size_t size,
                                    page_access_t access);
bool map_shared_memory(shmem_handle_t handle, size_t offset, void *start,
                       size_t size, page_access_t access);
bool unmap_shared_memory(shmem_handle_t handle, void *start, size_t size);
bool destroy_shared_memory(shmem_handle_t handle);

//
// access watches
//
struct memory_watch_s;

typedef enum {
  WATCH_SINGLE_WRITE,
} memory_watch_type_t;

typedef void (*memory_watch_cb)(const struct re_exception_s *, void *);

struct memory_watch_s *add_single_write_watch(void *ptr, size_t size,
                                              memory_watch_cb cb, void *data);
void remove_memory_watch(struct memory_watch_s *watch);

#ifdef __cplusplus
}
#endif

#endif
