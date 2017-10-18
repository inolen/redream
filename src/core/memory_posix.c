#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include "core/core.h"
#include "core/filesystem.h"
#include "core/list.h"
#include "core/memory.h"

#if PLATFORM_ANDROID
#include <linux/ashmem.h>
#endif

#define MAX_SHMEM 128

struct shmem {
  char filename[PATH_MAX];
  int handle;
  struct list_node free_it;
};

static int initialized;
static struct shmem shmem_pool[MAX_SHMEM];
static struct list free_shmem;

static mode_t access_to_mode_flags(enum page_access access) {
  switch (access) {
    case ACC_READONLY:
      return S_IRUSR;
    case ACC_READWRITE:
      return S_IRUSR | S_IWUSR;
    default:
      return 0;
  }
}

static int access_to_open_flags(enum page_access access) {
  switch (access) {
    case ACC_READONLY:
      return O_RDONLY;
    case ACC_READWRITE:
      return O_RDWR;
    default:
      return 0;
  }
}

static int access_to_protect_flags(enum page_access access) {
  switch (access) {
    case ACC_READONLY:
      return PROT_READ;
    case ACC_READWRITE:
      return PROT_READ | PROT_WRITE;
    case ACC_READWRITEEXEC:
      return PROT_READ | PROT_WRITE | PROT_EXEC;
    default:
      return PROT_NONE;
  }
}

int release_pages(void *ptr, size_t size) {
  return munmap(ptr, size) == 0;
}

void *reserve_pages(void *ptr, size_t size) {
  /* NOTE mmap with MAP_FIXED will overwrite existing mappings, making it hard
     to detect that a section of memory has already been mmap'd. however, mmap
     without MAP_FIXED will obey the address parameter only if an existing
     mapping does not already exist, else it will map it to a new address.
     knowing this, an existing mapping can be detected by not using MAP_FIXED,
     and comparing the returned mapped address with the requested address */
  void *res =
      mmap(ptr, size, PROT_NONE, MAP_SHARED | MAP_ANON | MAP_NORESERVE, -1, 0);

  if (res == MAP_FAILED) {
    return NULL;
  }

  if (ptr && res != ptr) {
    /* mapping was successful. however, it was made at a different address
       than requested, meaning the requested address has already been mapped */
    munmap(res, size);
    return NULL;
  }

  return res;
}

int protect_pages(void *ptr, size_t size, enum page_access access) {
  int prot = access_to_protect_flags(access);
  return mprotect(ptr, size, prot) == 0;
}

size_t get_allocation_granularity() {
  return get_page_size();
}

size_t get_page_size() {
  return getpagesize();
}

static void init_shared_memory_entries() {
  if (initialized) {
    return;
  }

  initialized = 1;

  /* add all entries to free list */
  for (int i = 0; i < MAX_SHMEM; i++) {
    struct shmem *shmem = &shmem_pool[i];
    list_add(&free_shmem, &shmem->free_it);
  }
}

int destroy_shared_memory(shmem_handle_t handle) {
  init_shared_memory_entries();

  struct shmem *shmem = (struct shmem *)handle;
  int res = 0;

#if PLATFORM_ANDROID
  res = close(shmem->handle);
#else
  int res1 = close(shmem->handle);
  int res2 = shm_unlink(shmem->filename);
  res = res1 == 0 && res2 == 0;
#endif

  /* add back to free list */
  list_add(&free_shmem, &shmem->free_it);

  return res;
}

int unmap_shared_memory(shmem_handle_t handle, void *start, size_t size) {
  return munmap(start, size) == 0;
}

void *map_shared_memory(shmem_handle_t handle, size_t offset, void *start,
                        size_t size, enum page_access access) {
  init_shared_memory_entries();

  struct shmem *shmem = (struct shmem *)handle;
  int prot = access_to_protect_flags(access);
  int flags = start ? MAP_SHARED | MAP_FIXED : MAP_SHARED;
  void *ptr = mmap(start, size, prot, flags, shmem->handle, offset);

  if (ptr == MAP_FAILED) {
    return SHMEM_MAP_FAILED;
  }

  return ptr;
}

shmem_handle_t create_shared_memory(const char *filename, size_t size,
                                    enum page_access access) {
  init_shared_memory_entries();

  /* find unused shmem entry (wrapper for both shmem object name and file
     handle) */
  struct shmem *shmem = list_first_entry(&free_shmem, struct shmem, free_it);
  CHECK_NOTNULL(shmem);

#if PLATFORM_ANDROID
  int oflag = access_to_open_flags(access);

  int handle = open("/" ASHMEM_NAME_DEF, oflag);
  if (handle < 0) {
    return NULL;
  }

  int ret = ioctl(handle, ASHMEM_SET_NAME, filename);
  if (ret < 0) {
    close(handle);
    return NULL;
  }

  ret = ioctl(handle, ASHMEM_SET_SIZE, size);
  if (ret < 0) {
    close(handle);
    return NULL;
  }
#else
  /* make sure the shared memory object doesn't already exist */
  shm_unlink(filename);

  /* create the shared memory object and open a file handle to it */
  int oflag = access_to_open_flags(access);
  mode_t mode = access_to_mode_flags(access);
  int handle = shm_open(filename, oflag | O_CREAT | O_EXCL, mode);
  if (handle == -1) {
    return NULL;
  }

  /* resize it */
  int res = ftruncate(handle, size);
  if (res == -1) {
    shm_unlink(filename);
    return NULL;
  }
#endif

  /* update entry, remove from free list */
  strncpy(shmem->filename, filename, sizeof(shmem->filename));
  shmem->handle = handle;
  list_remove(&free_shmem, &shmem->free_it);

  return (shmem_handle_t)shmem;
}
