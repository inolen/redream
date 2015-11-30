#ifndef MEMORY_H
#define MEMORY_H

#include <vector>
#include "core/interval_tree.h"
#include "sys/exception_handler.h"
#include "sys/memory.h"

namespace dreavm {
namespace hw {

typedef uint8_t RegionHandle;
typedef uint8_t (*R8Handler)(void *, uint32_t);
typedef uint16_t (*R16Handler)(void *, uint32_t);
typedef uint32_t (*R32Handler)(void *, uint32_t);
typedef uint64_t (*R64Handler)(void *, uint32_t);
typedef void (*W8Handler)(void *, uint32_t, uint8_t);
typedef void (*W16Handler)(void *, uint32_t, uint16_t);
typedef void (*W32Handler)(void *, uint32_t, uint32_t);
typedef void (*W64Handler)(void *, uint32_t, uint64_t);

enum {
  ADDRESS_SPACE_SIZE = (1ull << 32),
  UNMAPPED = (RegionHandle)0,
  PAGE_BITS = 20,
  OFFSET_BITS = 32 - PAGE_BITS,
  MAX_PAGE_SIZE = 1 << OFFSET_BITS,
  MAX_PAGES = 1 << PAGE_BITS,
  MAX_REGIONS = (1 << (sizeof(RegionHandle) * 8)) - 1,
};

struct MemoryRegion {
  uint32_t addr_mask;
  uint32_t logical_addr;
  uint32_t size;
  bool dynamic;
  bool mapped;

  // physical range
  uint8_t *data;

  // dynamic range
  void *ctx;
  R8Handler r8;
  R16Handler r16;
  R32Handler r32;
  R64Handler r64;
  W8Handler w8;
  W16Handler w16;
  W32Handler w32;
  W64Handler w64;
};

class MemoryMap {
 public:
  MemoryMap();

  MemoryRegion *page(int i) { return &regions_[pages_[i]]; }
  int num_pages() { return MAX_PAGES; }

  inline void Lookup(uint32_t logical_addr, MemoryRegion **out_range,
                     uint32_t *out_offset);

  void Mirror(uint32_t logical_addr, uint32_t size, uint32_t mirror_mask);
  void Handle(uint32_t logical_addr, uint32_t size, uint32_t mirror_mask,
              void *ctx, R8Handler r8, R16Handler r16, R32Handler r32,
              R64Handler r64, W8Handler w8, W16Handler w16, W32Handler w32,
              W64Handler w64);

 private:
  MemoryRegion *AllocRegion();
  void MapRegion(uint32_t addr, uint32_t size, MemoryRegion *range);

  RegionHandle pages_[MAX_PAGES];
  MemoryRegion regions_[MAX_REGIONS];
  int num_regions_;
};

enum WatchType {
  WATCH_ACCESS_FAULT,
  WATCH_SINGLE_WRITE,
};

typedef void (*WatchHandler)(void *, const sys::Exception &, void *);

struct Watch {
  WatchType type;
  WatchHandler handler;
  void *ctx;
  void *data;
  void *ptr;
  size_t size;
};

typedef IntervalTree<Watch> WatchTree;
typedef WatchTree::node_type *WatchHandle;

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

  Memory();
  ~Memory();

  uint8_t *physical_base() { return physical_base_; }
  uint8_t *virtual_base() { return virtual_base_; }
  size_t total_size() { return ADDRESS_SPACE_SIZE; }

  bool Init(const MemoryMap &map);
  void Lookup(uint32_t logical_addr, MemoryRegion **out_bank,
              uint32_t *out_offset);
  void Memcpy(uint32_t logical_dest, void *ptr, uint32_t size);
  void Memcpy(void *ptr, uint32_t logical_src, uint32_t size);

  // TODO move to sys/memory.cc
  WatchHandle AddSingleWriteWatch(void *ptr, size_t size, WatchHandler handler,
                                  void *ctx, void *data);
  void RemoveWatch(WatchHandle handle);

  uint8_t R8(uint32_t addr);
  uint16_t R16(uint32_t addr);
  uint32_t R32(uint32_t addr);
  uint64_t R64(uint32_t addr);
  void W8(uint32_t addr, uint8_t value);
  void W16(uint32_t addr, uint16_t value);
  void W32(uint32_t addr, uint32_t value);
  void W64(uint32_t addr, uint64_t value);

 private:
  // exception handler for protected page watches
  sys::ExceptionHandlerHandle exc_handler_;

  // shared memory object where all physical data is written to
  sys::SharedMemoryHandle shmem_;

  // base address of the 32-bit address space. the first is for direct access
  // to the shared memory, the second will trigger interrupts and call into
  // the appropriate virtual memory bank if available
  uint8_t *physical_base_;
  uint8_t *virtual_base_;

  // mapping of all logical addresses to their respective physical address or
  // dynamic handler
  MemoryMap map_;

  // interval tree of address ranges being watched
  WatchTree watches_;

  static bool HandleException(void *ctx, sys::Exception &ex);

  bool CreateAddressSpace();
  void DestroyAddressSpace();

  bool MapAddressSpace();
  void UnmapAddressSpace();

  template <typename INT, INT (*MemoryRegion::*HANDLER)(void *, uint32_t)>
  INT ReadBytes(uint32_t addr);
  template <typename INT, void (*MemoryRegion::*HANDLER)(void *, uint32_t, INT)>
  void WriteBytes(uint32_t addr, INT value);
};
}
}

#endif
