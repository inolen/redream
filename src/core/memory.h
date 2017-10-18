#ifndef SYS_MEMORY_H
#define SYS_MEMORY_H

#include <stddef.h>

struct exception_state;

/*
 * page protection
 */
enum page_access {
  ACC_NONE,
  ACC_READONLY,
  ACC_READWRITE,
  ACC_READWRITEEXEC,
};

size_t get_page_size();
size_t get_allocation_granularity();
int protect_pages(void *ptr, size_t size, enum page_access access);
void *reserve_pages(void *ptr, size_t size);
int release_pages(void *ptr, size_t size);

/*
 * shared memory objects
 */
typedef void *shmem_handle_t;
#define SHMEM_INVALID NULL
#define SHMEM_MAP_FAILED (void *)-1

shmem_handle_t create_shared_memory(const char *filename, size_t size,
                                    enum page_access access);
void *map_shared_memory(shmem_handle_t handle, size_t offset, void *start,
                        size_t size, enum page_access access);
int unmap_shared_memory(shmem_handle_t handle, void *start, size_t size);
int destroy_shared_memory(shmem_handle_t handle);

/*
 * access watches
 */
struct memory_watch;

enum memory_watch_type {
  WATCH_SINGLE_WRITE,
};

typedef void (*memory_watch_cb)(const struct exception_state *, void *);

struct memory_watch *add_single_write_watch(const void *ptr, size_t size,
                                            memory_watch_cb cb, void *data);
void remove_memory_watch(struct memory_watch *watch);

#endif
