#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unordered_map>
#include "core/core.h"
#include "sys/memory.h"

namespace dvm {
namespace sys {

static std::unordered_map<int, std::string> shared_handles;

static mode_t AccessToModeFlags(PageAccess access) {
  switch (access) {
    case ACC_READONLY:
      return S_IREAD;
    case ACC_READWRITE:
      return S_IREAD | S_IWRITE;
    default:
      return 0;
  }
}

static int AccessToOpenFlags(PageAccess access) {
  switch (access) {
    case ACC_READONLY:
      return O_RDONLY;
    case ACC_READWRITE:
      return O_RDWR;
    default:
      return 0;
  }
}

static int AccessToProtectionFlags(PageAccess access) {
  switch (access) {
    case ACC_READONLY:
      return PROT_READ;
    case ACC_READWRITE:
      return PROT_READ | PROT_WRITE;
    default:
      return PROT_NONE;
  }
}

size_t GetPageSize() { return getpagesize(); }

size_t GetAllocationGranularity() { return GetPageSize(); }

bool ProtectPages(void *ptr, size_t size, PageAccess access) {
  int prot = AccessToProtectionFlags(access);
  return mprotect(ptr, size, prot) == 0;
}

bool ReservePages(void *ptr, size_t size) {
  // NOTE mmap with MAP_FIXED will overwrite existing mappings, making it hard
  // to detect that a section of memory has already been mmap'd. however, mmap
  // without MAP_FIXED will obey the address parameter only if an existing
  // mapping does not already exist, else it will map it to a new address.
  // knowing this, an existing mapping can be detected by not using MAP_FIXED,
  // and comparing the returned mapped address with the requested address
  void *res =
      mmap(ptr, size, PROT_NONE, MAP_ANON | MAP_NORESERVE | MAP_PRIVATE, -1, 0);

  if (res == MAP_FAILED) {
    return false;
  }

  if (res != ptr) {
    // mapping was successful. however, it was made at a different address
    // than requested, meaning the requested address has already been mapped
    munmap(res, size);
    return false;
  }

  return true;
}

bool ReleasePages(void *ptr, size_t size) { return munmap(ptr, size) == 0; }

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

  shared_handles.insert(std::make_pair(handle, filename));

  return handle;
}

bool MapSharedMemory(SharedMemoryHandle handle, size_t offset, void *start,
                     size_t size, PageAccess access) {
  int prot = AccessToProtectionFlags(access);
  void *ptr = mmap(start, size, prot, MAP_SHARED | MAP_FIXED, handle, offset);
  return ptr != MAP_FAILED;
}

bool UnmapSharedMemory(SharedMemoryHandle handle, void *start, size_t size) {
  return munmap(start, size) == 0;
}

bool DestroySharedMemory(SharedMemoryHandle handle) {
  auto it = shared_handles.find(handle);
  if (it == shared_handles.end()) {
    return false;
  }

  // close the file handle
  int res1 = close(handle);

  // destroy the shared memory object
  const char *filename = it->second.c_str();
  int res2 = shm_unlink(filename);

  shared_handles.erase(it);

  return res1 == 0 && res2 == 0;
}
}
}
