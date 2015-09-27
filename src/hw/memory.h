#ifndef MEMORY_H
#define MEMORY_H

#include <vector>
#include "sys/memory.h"

namespace dreavm {
namespace hw {

typedef uint8_t TableHandle;
typedef bool (*VirtualHandler)(void *, uintptr_t, uintptr_t);
typedef uint8_t (*R8Handler)(void *, uint32_t);
typedef uint16_t (*R16Handler)(void *, uint32_t);
typedef uint32_t (*R32Handler)(void *, uint32_t);
typedef uint64_t (*R64Handler)(void *, uint32_t);
typedef void (*W8Handler)(void *, uint32_t, uint8_t);
typedef void (*W16Handler)(void *, uint32_t, uint16_t);
typedef void (*W32Handler)(void *, uint32_t, uint32_t);
typedef void (*W64Handler)(void *, uint32_t, uint64_t);

#define UNMAPPED ((TableHandle)0)
#define PAGE_BITS 20
#define OFFSET_BITS (32 - PAGE_BITS)
#define MAX_PAGE_SIZE (1 << OFFSET_BITS)
#define MAX_ENTRIES (1 << PAGE_BITS)
#define MAX_HANDLES ((1 << (sizeof(TableHandle) * 8)) - 1)
#define ADDRESS_SPACE_SIZE 0x100000000

class PageTable {
 public:
  PageTable();
  ~PageTable();

  TableHandle Lookup(uint32_t addr);
  void MapRange(uint32_t addr, uint32_t size, TableHandle handle);

 private:
  TableHandle *table_;
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

  void set_virtual_handler(VirtualHandler handler, void *ctx) {
    virtual_handler_ = handler;
    virtual_handler_ctx_ = ctx;
  };

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

  uint8_t R8(uint32_t addr);
  uint16_t R16(uint32_t addr);
  uint32_t R32(uint32_t addr);
  uint64_t R64(uint32_t addr);
  void W8(uint32_t addr, uint8_t value);
  void W16(uint32_t addr, uint16_t value);
  void W32(uint32_t addr, uint32_t value);
  void W64(uint32_t addr, uint64_t value);

 private:
  sys::SharedMemoryHandle shm_;
  uint8_t *physical_base_;
  uint8_t *virtual_base_;
  VirtualHandler virtual_handler_;
  void *virtual_handler_ctx_;

  PageTable table_;
  int num_banks_;
  MemoryBank banks_[MAX_HANDLES];

  static bool HandleAccessFault(void *ctx, void *data, uintptr_t rip,
                                uintptr_t fault_addr);

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
