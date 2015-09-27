#include <windows.h>
#include "core/core.h"
#include "sys/memory.h"

namespace dreavm {
namespace sys {

static DWORD AccessToFileFlags(PageAccess access) {
  switch (access) {
    case ACC_NONE:
      return 0;
    case ACC_READONLY:
      return FILE_MAP_READ;
    case ACC_READWRITE:
      return FILE_MAP_READ | FILE_MAP_WRITE;
  }

  return 0;
}

static DWORD AccessToProtectionFlags(PageAccess access) {
  switch (access) {
    case ACC_NONE:
      return PAGE_NOACCESS;
    case ACC_READONLY:
      return PAGE_READONLY;
    case ACC_READWRITE:
      return PAGE_READWRITE;
  }

  return PAGE_NOACCESS;
}

static void CheckPageAligned(void *ptr, size_t size) {
  size_t page_size = GetPageSize();
  CHECK_EQ(reinterpret_cast<uintptr_t>(ptr) % page_size, 0);
  CHECK_EQ((reinterpret_cast<uintptr_t>(ptr) + size) % page_size, 0);
}

size_t GetPageSize() {
  SYSTEM_INFO system_info;
  GetSystemInfo(&system_info);
  return system_info.dwPageSize;
}

bool ProtectPages(void *ptr, size_t size, PageAccess access) {
  CheckPageAligned(ptr, size);

  DWORD new_protect = AccessToProtectionFlags(access);
  DWORD old_protect;
  return VirtualProtect(ptr, size, new_protect, &old_protect) != 0;
}

bool ReservePages(void *ptr, size_t size) {
  CheckPageAligned(ptr, size);

  void *res = VirtualAlloc(ptr, size, MEM_RESERVE, PAGE_NOACCESS);
  return res && res == ptr;
}

bool ReleasePages(void *ptr, size_t size) {
  CheckPageAligned(ptr, size);

  return VirtualFree(ptr, 0, MEM_RELEASE) != 0;
}

SharedMemoryHandle CreateSharedMemory(const char *filename, size_t size,
                                      PageAccess access) {
  DWORD prot = AccessToProtectionFlags(access);
  return CreateFileMapping(INVALID_HANDLE_VALUE, nullptr, prot | SEC_RESERVE,
                           static_cast<DWORD>(size >> 32),
                           static_cast<DWORD>(size), filename);
}

bool MapSharedMemory(SharedMemoryHandle handle, void *start, size_t offset,
                     size_t size, PageAccess access) {
  CheckPageAligned(start, size);

  DWORD file_flags = AccessToFileFlags(access);
  void *ptr =
      MapViewOfFileEx(handle, file_flags, static_cast<DWORD>(offset >> 32),
                      static_cast<DWORD>(offset), size, start);
  if (ptr != start) {
    return false;
  }

  ptr = VirtualAlloc(start, size, MEM_COMMIT, PAGE_READWRITE);
  if (ptr != start) {
    return false;
  }

  return true;
}

bool UnmapSharedMemory(SharedMemoryHandle handle, void *start, size_t size) {
  CheckPageAligned(start, size);

  return UnmapViewOfFile(start) != 0;
}

bool DestroySharedMemory(SharedMemoryHandle handle) {
  return CloseHandle(handle) != 0;
}
}
}
