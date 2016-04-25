#ifndef MEMORY_H
#define MEMORY_H

#include <unordered_map>
#include "core/delegate.h"
#include "sys/exception_handler.h"
#include "sys/memory.h"

namespace re {
namespace hw {

class AddressMap;
class Machine;
class Memory;

typedef int MapEntryHandle;
typedef uint8_t RegionHandle;
typedef uintptr_t PageEntry;

typedef delegate<uint8_t(uint32_t)> R8Delegate;
typedef delegate<uint16_t(uint32_t)> R16Delegate;
typedef delegate<uint32_t(uint32_t)> R32Delegate;
typedef delegate<uint64_t(uint32_t)> R64Delegate;

typedef delegate<void(uint32_t, uint8_t)> W8Delegate;
typedef delegate<void(uint32_t, uint16_t)> W16Delegate;
typedef delegate<void(uint32_t, uint32_t)> W32Delegate;
typedef delegate<void(uint32_t, uint64_t)> W64Delegate;

static const uint64_t ADDRESS_SPACE_SIZE = 1ull << 32;

// helpers for extracting page information out of a virtual address
static const int PAGE_BITS = 20;
static const int PAGE_OFFSET_BITS = 32 - PAGE_BITS;
static const int PAGE_BLKSIZE = 1 << PAGE_OFFSET_BITS;
static const int NUM_PAGES = 1 << PAGE_BITS;
static const uint32_t PAGE_OFFSET_MASK = PAGE_BLKSIZE - 1;
static const uint32_t PAGE_INDEX_MASK = ~PAGE_OFFSET_MASK;

// helpers for accessing region information out of a page table entry
static const int MAX_REGIONS = 1 << (PAGE_OFFSET_BITS - 1);
static const uintptr_t REGION_INDEX_MASK = MAX_REGIONS - 1;
static const uintptr_t REGION_TYPE_MASK = MAX_REGIONS;
static const uintptr_t REGION_OFFSET_MASK =
    ~(REGION_TYPE_MASK | REGION_INDEX_MASK);

struct MemoryRegion {
  RegionHandle handle;
  uint32_t shmem_offset;
  uint32_t size;
  bool dynamic;

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
  friend class AddressSpace;

 public:
  Memory(Machine &machine);
  ~Memory();

  bool Init();

  RegionHandle CreateRegion(uint32_t size);
  RegionHandle CreateRegion(uint32_t size, R8Delegate r8, R16Delegate r16,
                            R32Delegate r32, R64Delegate r64, W8Delegate w8,
                            W16Delegate w16, W32Delegate w32, W64Delegate w64);

 private:
  bool CreateSharedMemory();
  void DestroySharedMemory();

  MemoryRegion &AllocRegion(uint32_t size);

  Machine &machine_;
  sys::SharedMemoryHandle shmem_;
  uint32_t shmem_size_;

  MemoryRegion regions_[MAX_REGIONS];
  int num_regions_;
};

// clang-format off

// macros to help generate static AddressMap creators
#define AM_DECLARE(name)                                                            \
  static void name(void *, Machine &, AddressMap &, uint32_t);

#define AM_BEGIN(type, name)                                                        \
  void type::name(void *that, Machine &machine, AddressMap &map, uint32_t offset) { \
    type *self = static_cast<type *>(that);                                         \
    Memory *memory = machine.memory();                                              \
    uint32_t begin_addr = 0;                                                        \
    uint32_t end_addr = 0;                                                          \
    uint32_t size = 0;                                                              \
    RegionHandle region = 0;                                                        \
    Device *device = nullptr;                                                       \
    (void)self;                                                                     \
    (void)memory;                                                                   \
    (void)begin_addr;                                                               \
    (void)end_addr;                                                                 \
    (void)size;                                                                     \
    (void)region;                                                                   \
    (void)device;
#define AM_RANGE(begin, end)                                                        \
    begin_addr = offset + begin;                                                    \
    end_addr = offset + end;                                                        \
    size = end_addr - begin_addr + 1;
#define AM_MOUNT()                                                                  \
    region = memory->CreateRegion(size);                                            \
    map.Mount(region, size, begin_addr);                                            \
    region = 0;
#define AM_HANDLE(r8_, r16_, r32_, r64_, w8_, w16_, w32_, w64_)                     \
    region = memory->CreateRegion(size,                                             \
                                 delegate<uint8_t(uint32_t)>(self, r8_),            \
                                 delegate<uint16_t(uint32_t)>(self, r16_),          \
                                 delegate<uint32_t(uint32_t)>(self, r32_),          \
                                 delegate<uint64_t(uint32_t)>(self, r64_),          \
                                 delegate<void(uint32_t, uint8_t)>(self, w8_) ,     \
                                 delegate<void(uint32_t, uint16_t)>(self, w16_),    \
                                 delegate<void(uint32_t, uint32_t)>(self, w32_),    \
                                 delegate<void(uint32_t, uint64_t)>(self, w64_));   \
    map.Mount(region, size, begin_addr);                                            \
    region = 0;
#define AM_DEVICE(name, type, cb)                                                   \
    device = machine.LookupDevice(name);                                            \
    CHECK_NOTNULL(device);                                                          \
    type::cb(device, machine, map, begin_addr);                                     \
    device = nullptr;
#define AM_MIRROR(addr)                                                             \
    map.Mirror(addr, size, begin_addr);
#define AM_END()                                                                    \
  }

// clang-format on

enum MapEntryType {
  MAP_ENTRY_MOUNT,
  MAP_ENTRY_MIRROR,
};

struct MapEntry {
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

typedef void (*AddressMapper)(void *, Machine &, AddressMap &, uint32_t);

class AddressMap {
 public:
  AddressMap();

  const MapEntry *entry(int i) const { return &entries_[i]; }
  int num_entries() const { return num_entries_; }

  void Mount(RegionHandle handle, uint32_t size, uint32_t virtual_addr);
  void Mirror(uint32_t physical_addr, uint32_t size, uint32_t virtual_addr);

 private:
  MapEntry &AllocEntry();

  MapEntry entries_[MAX_REGIONS];
  int num_entries_;
};

class AddressSpace {
 public:
  static uint8_t R8(void *space, uint32_t addr);
  static uint16_t R16(void *space, uint32_t addr);
  static uint32_t R32(void *space, uint32_t addr);
  static uint64_t R64(void *space, uint32_t addr);
  static void W8(void *space, uint32_t addr, uint8_t value);
  static void W16(void *space, uint32_t addr, uint16_t value);
  static void W32(void *space, uint32_t addr, uint32_t value);
  static void W64(void *space, uint32_t addr, uint64_t value);

  AddressSpace(Memory &memory);
  ~AddressSpace();

  uint8_t *base() { return base_; }
  uint8_t *protected_base() { return protected_base_; }

  bool Map(const AddressMap &map);
  void Unmap();

  uint8_t *Translate(uint32_t addr);
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
  void CreatePageTable(const AddressMap &map);
  uint32_t GetPageOffset(const PageEntry &page) const;
  int GetNumAdjacentPages(int page_index) const;
  bool MapPageTable(uint8_t *base);
  void UnmapPageTable(uint8_t *base);

  template <typename INT, delegate<INT(uint32_t)> MemoryRegion::*DELEGATE>
  INT ReadBytes(uint32_t addr);
  template <typename INT, delegate<void(uint32_t, INT)> MemoryRegion::*DELEGATE>
  void WriteBytes(uint32_t addr, INT value);

  Memory &memory_;
  PageEntry pages_[NUM_PAGES];
  uint8_t *base_;
  uint8_t *protected_base_;
};
}
}

#endif
