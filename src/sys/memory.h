#ifndef SYS_MEMORY_H
#define SYS_MEMORY_H

#include "core/delegate.h"
#include "core/interval_tree.h"
#include "sys/exception_handler.h"

namespace re {
namespace sys {

//
// page protection
//
enum PageAccess {
  ACC_NONE,
  ACC_READONLY,
  ACC_READWRITE,
};

size_t GetPageSize();
size_t GetAllocationGranularity();
bool ProtectPages(void *ptr, size_t size, PageAccess access);
bool ReservePages(void *ptr, size_t size);
bool ReleasePages(void *ptr, size_t size);

//
// shared memory objects
//
#if PLATFORM_WINDOWS
typedef void *SharedMemoryHandle;
#define SHMEM_INVALID nullptr
#else
typedef int SharedMemoryHandle;
#define SHMEM_INVALID -1
#endif

SharedMemoryHandle CreateSharedMemory(const char *filename, size_t size,
                                      PageAccess access);
bool MapSharedMemory(SharedMemoryHandle handle, size_t offset, void *start,
                     size_t size, PageAccess access);
bool UnmapSharedMemory(SharedMemoryHandle handle, void *start, size_t size);
bool DestroySharedMemory(SharedMemoryHandle handle);

//
// access watches
//
enum WatchType {
  WATCH_ACCESS_FAULT,
  WATCH_SINGLE_WRITE,
};

typedef delegate<void(const sys::Exception &, void *)> WatchDelegate;

struct Watch {
  WatchType type;
  WatchDelegate delegate;
  void *data;
};

typedef IntervalTree<Watch> WatchTree;
typedef WatchTree::node_type *WatchHandle;

WatchHandle AddSingleWriteWatch(void *ptr, size_t size, WatchDelegate delegate,
                                void *data);
void RemoveAccessWatch(WatchHandle handle);
}
}

#endif
