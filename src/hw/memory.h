#ifndef MEMORY_H
#define MEMORY_H

#include <vector>
#include "core/interval_tree.h"
#include "sys/exception_handler.h"
#include "sys/memory.h"

namespace dreavm {
namespace hw {

#define ADDRESS_SPACE_SIZE 0x100000000

// virtual memory page table
typedef uint8_t TableHandle;
typedef uint8_t (*R8Handler)(void *, uint32_t);
typedef uint16_t (*R16Handler)(void *, uint32_t);
typedef uint32_t (*R32Handler)(void *, uint32_t);
typedef uint64_t (*R64Handler)(void *, uint32_t);
typedef void (*W8Handler)(void *, uint32_t, uint8_t);
typedef void (*W16Handler)(void *, uint32_t, uint16_t);
typedef void (*W32Handler)(void *, uint32_t, uint32_t);
typedef void (*W64Handler)(void *, uint32_t, uint64_t);

enum {
  UNMAPPED = (TableHandle)0,
  PAGE_BITS = 20,
  OFFSET_BITS = 32 - PAGE_BITS,
  MAX_PAGE_SIZE = 1 << OFFSET_BITS,
  MAX_ENTRIES = 1 << PAGE_BITS,
  MAX_HANDLES = (1 << (sizeof(TableHandle) * 8)) - 1
};

struct MemoryBank {
  TableHandle handle;
  uint32_t addr_mask;
  uint32_t logical_addr;
  uint32_t size;
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

class PageTable {
 public:
  PageTable();
  ~PageTable();

  TableHandle Lookup(uint32_t addr);
  void MapRange(uint32_t addr, uint32_t size, TableHandle handle);

 private:
  TableHandle *table_;
};

// access watches
enum WatchType { WATCH_ACCESS_FAULT, WATCH_SINGLE_WRITE };

typedef void (*WatchHandler)(void *, const sys::Exception &, void *);

struct Watch {
  Watch(WatchType type, WatchHandler handler, void *ctx, void *data, void *ptr,
        size_t size)
      : type(type),
        handler(handler),
        ctx(ctx),
        data(data),
        ptr(ptr),
        size(size) {}

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

  bool Init();
  bool Resolve(uint32_t logical_addr, MemoryBank **out_bank,
               uint32_t *out_offset);
  uint8_t *Alloc(uint32_t logical_addr, uint32_t size, uint32_t mirror_mask);
  void Handle(uint32_t logical_addr, uint32_t size, uint32_t mirror_mask,
              void *ctx, R8Handler r8, R16Handler r16, R32Handler r32,
              R64Handler r64, W8Handler w8, W16Handler w16, W32Handler w32,
              W64Handler w64);
  void Memcpy(uint32_t logical_dest, void *ptr, uint32_t size);
  void Memcpy(void *ptr, uint32_t logical_src, uint32_t size);

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
  sys::ExceptionHandlerHandle eh_handle_;

  // shared memory object where the allocated memory is committed to
  sys::SharedMemoryHandle shmem_handle_;

  // base address of the 32-bit address space. the first is for direct access
  // to the shared memory, the second will trigger interrupts and call into
  // the appropriate virtual memory bank if available
  uint8_t *physical_base_;
  uint8_t *virtual_base_;

  // table of virtual memory banks that've been mapped
  PageTable table_;
  MemoryBank banks_[MAX_HANDLES];
  int num_banks_;

  // interval tree of address ranges being watched
  WatchTree watches_;

  static bool HandleException(void *ctx, sys::Exception &ex);

  bool CreateAddressSpace();
  void DestroyAddressSpace();

  MemoryBank &AllocBank();

  template <typename INT, INT (*MemoryBank::*HANDLER)(void *, uint32_t)>
  INT ReadBytes(uint32_t addr);
  template <typename INT, void (*MemoryBank::*HANDLER)(void *, uint32_t, INT)>
  void WriteBytes(uint32_t addr, INT value);
};
}
}

#endif
