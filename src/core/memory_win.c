#include <windows.h>
#include "core/memory.h"

static DWORD access_to_file_flags(enum page_access access) {
  switch (access) {
    case ACC_READONLY:
      return FILE_MAP_READ;
    case ACC_READWRITE:
      return FILE_MAP_READ | FILE_MAP_WRITE;
    default:
      return 0;
  }
}

static DWORD access_to_protection_flags(enum page_access access) {
  switch (access) {
    case ACC_READONLY:
      return PAGE_READONLY;
    case ACC_READWRITE:
      return PAGE_READWRITE;
    case ACC_READWRITEEXEC:
      return PAGE_EXECUTE_READWRITE;
    default:
      return PAGE_NOACCESS;
  }
}

int release_pages(void *ptr, size_t size) {
  return VirtualFree(ptr, 0, MEM_RELEASE) != 0;
}

void *reserve_pages(void *ptr, size_t size) {
  void *res = VirtualAlloc(ptr, size, MEM_RESERVE, PAGE_NOACCESS);

  if (!res) {
    return NULL;
  }

  if (ptr && res != ptr) {
    /* mapping was successful, however it was made at a different address than
       requested */
    VirtualFree(ptr, 0, MEM_RELEASE);
    return NULL;
  }

  return res;
}

int protect_pages(void *ptr, size_t size, enum page_access access) {
  DWORD new_protect = access_to_protection_flags(access);
  DWORD old_protect;
  return VirtualProtect(ptr, size, new_protect, &old_protect) != 0;
}

size_t get_allocation_granularity() {
  SYSTEM_INFO si;
  GetSystemInfo(&si);
  return si.dwAllocationGranularity;
}

size_t get_page_size() {
  SYSTEM_INFO si;
  GetSystemInfo(&si);
  return si.dwPageSize;
}

int destroy_shared_memory(shmem_handle_t handle) {
  return CloseHandle(handle) != 0;
}

int unmap_shared_memory(shmem_handle_t handle, void *start, size_t size) {
  return UnmapViewOfFile(start) != 0;
}

void *map_shared_memory(shmem_handle_t handle, size_t offset, void *start,
                        size_t size, enum page_access access) {
  DWORD file_flags = access_to_file_flags(access);
  void *ptr = MapViewOfFileEx(handle, file_flags, (DWORD)(offset >> 32),
                              (DWORD)offset, size, start);
  if ((start && ptr != start) || (!start && !ptr)) {
    return SHMEM_MAP_FAILED;
  }

  /* commit the pages backing the file mapping now */
  DWORD protect = access_to_protection_flags(access);
  ptr = VirtualAlloc(ptr, size, MEM_COMMIT, protect);
  if (!ptr) {
    return SHMEM_MAP_FAILED;
  }

  return ptr;
}

shmem_handle_t create_shared_memory(const char *filename, size_t size,
                                    enum page_access access) {
  DWORD protect = access_to_protection_flags(access);
  /* note, only reserve the pages for now */
  return CreateFileMapping(INVALID_HANDLE_VALUE, NULL, protect | SEC_RESERVE,
                           (DWORD)(size >> 32), (DWORD)(size), filename);
}
