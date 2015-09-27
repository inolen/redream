#ifndef SYS_MEMORY_H
#define SYS_MEMORY_H

#include "core/platform.h"

namespace dreavm {
namespace sys {

#ifdef PLATFORM_WINDOWS
typedef void *SharedMemoryHandle;
#define SHMEM_INVALID nullptr
#else
typedef int SharedMemoryHandle;
#define SHMEM_INVALID -1
#endif

enum AllocationType { ALLOC_RESERVE, ALLOC_COMMIT };
enum PageAccess { ACC_NONE, ACC_READONLY, ACC_READWRITE };

size_t GetPageSize();
bool ProtectPages(void *ptr, size_t size, PageAccess access);
bool ReservePages(void *ptr, size_t size);
bool ReleasePages(void *ptr, size_t size);

SharedMemoryHandle CreateSharedMemory(const char *filename, size_t size,
                                      PageAccess access);
bool MapSharedMemory(SharedMemoryHandle handle, void *start, size_t offset,
                     size_t size, PageAccess access);
bool UnmapSharedMemory(SharedMemoryHandle handle, void *start, size_t size);
bool DestroySharedMemory(SharedMemoryHandle handle);
}
}

#endif
