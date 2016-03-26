#ifndef MEMORY_H
#define MEMORY_H

#include <functional>
#include "core/delegate.h"
#include "sys/exception_handler.h"
#include "sys/memory.h"

namespace re {
namespace hw {

class Machine;

typedef uint8_t RegionHandle;

enum {
  MAX_REGIONS = (1 << (sizeof(RegionHandle) * 8)),
};

// Establish a mapping of physical memory regions to a virtual address space.
typedef int MapEntryHandle;

enum MapEntryType {
  MAP_ENTRY_MOUNT,
  MAP_ENTRY_MIRROR,
};

struct MapEntry {
  MapEntryHandle handle;
  MapEntryType type;

  union {
    struct {
      RegionHandle handle;
      uint32_t size;
      uint32_t virtual_addr;
    } mount;

    struct {
      uint32_t physical_addr;
      uint32_t size;
      uint32_t virtual_addr;
    } mirror;
  };
};

class MemoryMap {
 public:
  MemoryMap();

  const MapEntry *entry(int i) const { return &entries_[i]; }
  int num_entries() const { return num_entries_; }

  // physical regions can be allocated through Memory::AllocRegion, and the
  // returned handle mounted here
  void Mount(RegionHandle handle, uint32_t virtual_addr, uint32_t size);

  // mirror arbitary, page-aligned ranges of memory to a virtual address
  void Mirror(uint32_t physical_addr, uint32_t size, uint32_t virtual_addr);

 private:
  MapEntry *AllocEntry();

  MapEntry entries_[MAX_REGIONS];
  int num_entries_;
};

// The memory system reserves a 4gb aligned chunk of memory in which the entire
// 32-bit guest address space is subdivided into aligned pages.
//
// Regions of memory can be allocated inside the 32-bit address space with
// Memory::AllocRegion. Allocated regions are not immediately available for
// access, but can be mounted with AddressMap::Mount / AddressMap::Mirror and
// Memory::MapRegions to bring the region into the accessible address space.
//
// Each page entry in the page table is represented by a uintptr_t value. In
// the case of static pages which map directly to host ram, the value is just a
// pointer to the start of the aligned page in host memory. In the case that
// the page doesn't map directly to host ram, the low order offset bits are
// set to denote that the page is a dynamic page. For these pages, the entry
// is a packed bitstring of the format:
//   oooooooooooooooooooooooooooooooooooooooooooooooooooooooorrrrrrrr
// 'o' represents an offset into the region backing the page and 'r' represents
// the handle of the region backing the page.
//
// NOTE as a side effect of this, since static pages are a host pointer, and
// dynamic pages have a 1-indexed region handle encoded in them, valid page
// entries are always non-zero
typedef uintptr_t PageEntry;

typedef delegate<uint8_t(uint32_t)> R8Delegate;
typedef delegate<uint16_t(uint32_t)> R16Delegate;
typedef delegate<uint32_t(uint32_t)> R32Delegate;
typedef delegate<uint64_t(uint32_t)> R64Delegate;

typedef delegate<void(uint32_t, uint8_t)> W8Delegate;
typedef delegate<void(uint32_t, uint16_t)> W16Delegate;
typedef delegate<void(uint32_t, uint32_t)> W32Delegate;
typedef delegate<void(uint32_t, uint64_t)> W64Delegate;

enum {
  PAGE_BITS = 20,
  PAGE_OFFSET_BITS = 32 - PAGE_BITS,
  PAGE_BLKSIZE = 1 << PAGE_OFFSET_BITS,
  NUM_PAGES = 1 << PAGE_BITS
};

enum : uint64_t {
  ADDRESS_SPACE_SIZE = (1ull << 32),
};

enum : uint32_t {
  PAGE_INDEX_MASK =
      static_cast<uint32_t>(((1 << PAGE_BITS) - 1) << PAGE_OFFSET_BITS),
  PAGE_OFFSET_MASK = (1 << PAGE_OFFSET_BITS) - 1,
};

enum : PageEntry {
  PAGE_REGION_MASK = MAX_REGIONS - 1,
  PAGE_REGION_OFFSET_MASK = ~PAGE_REGION_MASK
};

// For static page entries, the entry itself is a page-aligned pointer to host
// memory. Being that the pointer is aligned, the low order offset bits are
// unset. This is exploited, and a bit is set in these low order bits to
// differentiate dynamic entries. Make sure that future page size adjustments
// don't shift this flag out of the offset bits, and into the page index bits.
static_assert((PAGE_INDEX_MASK & PAGE_REGION_MASK) == 0,
              "Dynamic bit intersects page index bits.");

struct MemoryRegion {
  RegionHandle handle;
  bool dynamic;
  uint32_t physical_addr;
  uint32_t size;
  void *ctx;

  R8Delegate r8;
  R16Delegate r16;
  R32Delegate r32;
  R64Delegate r64;
  W8Delegate w8;
  W16Delegate w16;
  W32Delegate w32;
  W64Delegate w64;
};

class Memory {
 public:
  static uint8_t R8(Memory *memory, uint32_t addr);
  static uint16_t R16(Memory *memory, uint32_t addr);
  static uint32_t R32(Memory *memory, uint32_t addr);
  static uint64_t R64(Memory *memory, uint32_t addr);
  static void W8(Memory *memory, uint32_t addr, uint8_t value);
  static void W16(Memory *memory, uint32_t addr, uint16_t value);
  static void W32(Memory *memory, uint32_t addr, uint32_t value);
  static void W64(Memory *memory, uint32_t addr, uint64_t value);

  Memory(Machine &machine);
  ~Memory();

  size_t total_size() { return ADDRESS_SPACE_SIZE; }
  uint8_t *virtual_base() { return virtual_base_; }
  uint8_t *protected_base() { return protected_base_; }

  bool Init();

  RegionHandle AllocRegion(uint32_t physical_addr, uint32_t size);
  RegionHandle AllocRegion(uint32_t physical_addr, uint32_t size, R8Delegate r8,
                           R16Delegate r16, R32Delegate r32, R64Delegate r64,
                           W8Delegate w8, W16Delegate w16, W32Delegate w32,
                           W64Delegate w64);

  uint8_t *TranslateVirtual(uint32_t addr);
  uint8_t *TranslateProtected(uint32_t addr);
  uint8_t R8(uint32_t addr);
  uint16_t R16(uint32_t addr);
  uint32_t R32(uint32_t addr);
  uint64_t R64(uint32_t addr);
  void W8(uint32_t addr, uint8_t value);
  void W16(uint32_t addr, uint16_t value);
  void W32(uint32_t addr, uint32_t value);
  void W64(uint32_t addr, uint64_t value);
  void Memcpy(uint32_t virtual_dest, const void *ptr, uint32_t size);
  void Memcpy(void *ptr, uint32_t virtual_src, uint32_t size);
  void Memcpy(uint32_t virtual_dest, uint32_t virtual_src, uint32_t size);
  void Lookup(uint32_t virtual_addr, uint8_t **ptr, MemoryRegion **region,
              uint32_t *offset);

 private:
  bool CreateSharedMemory();
  void DestroySharedMemory();

  bool ReserveAddressSpace(uint8_t **base);

  MemoryRegion *AllocRegion();

  bool Map(const MemoryMap &map);
  void Unmap();

  bool GetNextContiguousRegion(uint32_t *physical_start,
                               uint32_t *physical_end);
  bool MapPhysicalSpace();
  void UnmapPhysicalSpace();

  int GetNumAdjacentPhysicalPages(int page_index);
  uint32_t GetPhysicalAddress(const PageEntry &page);
  bool CreatePageTable(const MemoryMap &map);
  bool MapVirtualSpace();
  void UnmapVirtualSpace();

  template <typename INT, delegate<INT(uint32_t)> MemoryRegion::*DELEGATE>
  INT ReadBytes(uint32_t addr);
  template <typename INT, delegate<void(uint32_t, INT)> MemoryRegion::*DELEGATE>
  void WriteBytes(uint32_t addr, INT value);

  Machine &machine_;

  // shared memory object where all physical data is written to
  sys::SharedMemoryHandle shmem_;

  // physical regions of memory
  int num_regions_;
  MemoryRegion *regions_;

  // map virtual addresses -> physical addresses
  PageEntry *pages_;

  // base addresses for the 32-bit address space. physical base is where
  // the physical regions are mapped, virtual base is where the page table
  // gets mapped to, and protected base is the same as virtual, but with
  // dynamic regions mprotected so they segfaul on access
  uint8_t *physical_base_;
  uint8_t *virtual_base_;
  uint8_t *protected_base_;
};
}
}

#endif
