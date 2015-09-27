#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unordered_map>
#include "core/core.h"
#include "sys/memory.h"

namespace dreavm {
namespace sys {

std::unordered_map<int, std::string> g_shared_handles;

static mode_t AccessToModeFlags(PageAccess access) {
  switch (access) {
    case ACC_NONE:
      return 0;
    case ACC_READONLY:
      return S_IREAD;
    case ACC_READWRITE:
      return S_IREAD | S_IWRITE;
  }
}

static int AccessToOpenFlags(PageAccess access) {
  switch (access) {
    case ACC_NONE:
      return 0;
    case ACC_READONLY:
      return O_RDONLY;
    case ACC_READWRITE:
      return O_RDWR;
  }
}

static int AccessToProtectionFlags(PageAccess access) {
  switch (access) {
    case ACC_NONE:
      return PROT_NONE;
    case ACC_READONLY:
      return PROT_READ;
    case ACC_READWRITE:
      return PROT_READ | PROT_WRITE;
  }
}

static void CheckPageAligned(void *ptr, size_t size) {
  size_t page_size = GetPageSize();
  CHECK_EQ(reinterpret_cast<uintptr_t>(ptr) % page_size, 0);
  CHECK_EQ((reinterpret_cast<uintptr_t>(ptr) + size) % page_size, 0);
}

size_t GetPageSize() { return getpagesize(); }

bool ProtectPages(void *ptr, size_t size, PageAccess access) {
  CheckPageAligned(ptr, size);

  int prot = AccessToProtectionFlags(access);
  return mprotect(ptr, size, prot) == 0;
}

bool ReservePages(void *ptr, size_t size) {
  CheckPageAligned(ptr, size);

  void *res = mmap(ptr, size, PROT_NONE,
                   MAP_ANON | MAP_NORESERVE | MAP_PRIVATE | MAP_FIXED, -1, 0);
  return res != MAP_FAILED;
}

bool ReleasePages(void *ptr, size_t size) {
  CheckPageAligned(ptr, size);

  return munmap(ptr, size) == 0;
}

SharedMemoryHandle CreateSharedMemory(const char *filename, size_t size,
                                      PageAccess access) {
  // make sure the shared memory object doesn't already exist
  shm_unlink(filename);

  // create the shared memory object and open a file handle to it
  int oflag = AccessToOpenFlags(access);
  mode_t mode = AccessToModeFlags(access);
  int handle = shm_open(filename, oflag | O_CREAT | O_EXCL, mode);
  if (handle == -1) {
    return -1;
  }

  // resize it
  int res = ftruncate(handle, size);
  if (res == -1) {
    shm_unlink(filename);
    return -1;
  }

  g_shared_handles.insert(std::make_pair(handle, filename));

  return handle;
}

bool MapSharedMemory(SharedMemoryHandle handle, void *start, size_t offset,
                     size_t size, PageAccess access) {
  CheckPageAligned(start, size);

  int prot = AccessToProtectionFlags(access);
  void *ptr = mmap(start, size, prot, MAP_SHARED | MAP_FIXED, handle, offset);
  return ptr != MAP_FAILED;
}

bool UnmapSharedMemory(SharedMemoryHandle handle, void *start, size_t size) {
  CheckPageAligned(start, size);

  return munmap(start, size) == 0;
}

bool DestroySharedMemory(SharedMemoryHandle handle) {
  auto it = g_shared_handles.find(handle);
  CHECK_NE(it, g_shared_handles.end());

  // close the file handle
  int res1 = close(handle);

  // destroy the shared memory object
  const char *filename = it->second.c_str();
  int res2 = shm_unlink(filename);

  g_shared_handles.erase(it);

  return res1 == 0 && res2 == 0;
}
}
}
