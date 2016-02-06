#include <windows.h>
#include "sys/memory.h"

namespace re {
namespace sys {

static DWORD AccessToFileFlags(PageAccess access) {
  switch (access) {
    case ACC_READONLY:
      return FILE_MAP_READ;
    case ACC_READWRITE:
      return FILE_MAP_READ | FILE_MAP_WRITE;
    default:
      return 0;
  }
}

static DWORD AccessToProtectionFlags(PageAccess access) {
  switch (access) {
    case ACC_READONLY:
      return PAGE_READONLY;
    case ACC_READWRITE:
      return PAGE_READWRITE;
    default:
      return PAGE_NOACCESS;
  }
}

size_t GetPageSize() {
  SYSTEM_INFO si;
  GetSystemInfo(&si);
  return si.dwPageSize;
}

size_t GetAllocationGranularity() {
  SYSTEM_INFO si;
  GetSystemInfo(&si);
  return si.dwAllocationGranularity;
}

bool ProtectPages(void *ptr, size_t size, PageAccess access) {
  DWORD new_protect = AccessToProtectionFlags(access);
  DWORD old_protect;
  return VirtualProtect(ptr, size, new_protect, &old_protect) != 0;
}

bool ReservePages(void *ptr, size_t size) {
  void *res = VirtualAlloc(ptr, size, MEM_RESERVE, PAGE_NOACCESS);
  return res && res == ptr;
}

bool ReleasePages(void *ptr, size_t size) {
  return VirtualFree(ptr, 0, MEM_RELEASE) != 0;
}

SharedMemoryHandle CreateSharedMemory(const char *filename, size_t size,
                                      PageAccess access) {
  DWORD protect = AccessToProtectionFlags(access);
  return CreateFileMapping(INVALID_HANDLE_VALUE, nullptr, protect | SEC_COMMIT,
                           static_cast<DWORD>(size >> 32),
                           static_cast<DWORD>(size), filename);
}

bool MapSharedMemory(SharedMemoryHandle handle, size_t offset, void *start,
                     size_t size, PageAccess access) {
  DWORD file_flags = AccessToFileFlags(access);
  void *ptr =
      MapViewOfFileEx(handle, file_flags, static_cast<DWORD>(offset >> 32),
                      static_cast<DWORD>(offset), size, start);

  return ptr == start;
}

bool UnmapSharedMemory(SharedMemoryHandle handle, void *start, size_t size) {
  return UnmapViewOfFile(start) != 0;
}

bool DestroySharedMemory(SharedMemoryHandle handle) {
  return CloseHandle(handle) != 0;
}
}
}
