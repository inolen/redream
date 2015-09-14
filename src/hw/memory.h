#ifndef MEMORY_H
#define MEMORY_H

#include <math.h>
#include <stdlib.h>
#include <array>
#include <functional>
#include <list>
#include <sstream>
#include "core/core.h"

namespace dreavm {
namespace hw {

//
// single level page table implementation
//
typedef uint8_t TableHandle;

enum {
  UNMAPPED = (TableHandle)0,
  PAGE_BITS = 22,
  OFFSET_BITS = 32 - PAGE_BITS,
  MAX_PAGE_SIZE = 1 << OFFSET_BITS,
  MAX_ENTRIES = 1 << PAGE_BITS,
  MAX_HANDLES = (1 << (sizeof(TableHandle) * 8)) - 1
};

class PageTable {
 public:
  PageTable() {
    table_ = new TableHandle[MAX_ENTRIES];

    for (int i = 0; i < MAX_ENTRIES; i++) {
      table_[i] = UNMAPPED;
    }
  }

  ~PageTable() { delete[] table_; }

  inline TableHandle Lookup(uint32_t addr) {
    return table_[addr >> OFFSET_BITS];
  }

  // from a hardware perspective, the mirror mask parameter describes the
  // address bits which are ignored for the memory bank being mapped.
  //
  // from our perspective however, each permutation of these bits describes
  // a mirror for the memory bank being mapped.
  //
  // for example, on the dreamcast bits 29-31 are ignored for each address.
  // this means that 0x00040000 is also available at 0x20040000, 0x40040000,
  // 0x60040000, 0x80040000, 0xa0040000, 0xc0040000 and 0xe0040000.
  void MapRange(uint32_t start, uint32_t end, uint32_t mirror_mask,
                TableHandle handle) {
    if (!mirror_mask) {
      MapRange(start, end, handle);
      return;
    }

    int n = 31 - dreavm::clz(mirror_mask);
    uint32_t next_mask = mirror_mask & ~(1 << n);

    start |= mirror_mask;
    end |= mirror_mask;
    MapRange(start, end, next_mask, handle);

    start &= ~(1 << n);
    end &= ~(1 << n);
    MapRange(start, end, next_mask, handle);
  }

 private:
  void MapRange(uint32_t start, uint32_t end, TableHandle handle) {
    // ensure start and end are page aligned
    CHECK_EQ(start & (OFFSET_BITS - 1), 0);
    CHECK_EQ((end + 1) & (OFFSET_BITS - 1), 0);
    CHECK_LT(start, end);

    int l1_start = start >> OFFSET_BITS;
    int l1_end = end >> OFFSET_BITS;

    for (int i = l1_start; i <= l1_end; i++) {
      table_[i] = handle;
    }
  }

  TableHandle *table_;
};

//
// physical memory emulation
//
typedef std::function<uint8_t(uint32_t)> R8Handler;
typedef std::function<uint16_t(uint32_t)> R16Handler;
typedef std::function<uint32_t(uint32_t)> R32Handler;
typedef std::function<uint64_t(uint32_t)> R64Handler;
typedef std::function<void(uint32_t, uint8_t)> W8Handler;
typedef std::function<void(uint32_t, uint16_t)> W16Handler;
typedef std::function<void(uint32_t, uint32_t)> W32Handler;
typedef std::function<void(uint32_t, uint64_t)> W64Handler;

struct MemoryBank {
  MemoryBank()
      : handle(UNMAPPED),
        mirror_mask(0),
        logical_addr(0),
        physical_addr(nullptr),
        r8(nullptr),
        r16(nullptr),
        r32(nullptr),
        r64(nullptr),
        w8(nullptr),
        w16(nullptr),
        w32(nullptr),
        w64(nullptr) {}

  TableHandle handle;
  uint32_t mirror_mask;
  uint32_t logical_addr;
  uint8_t *physical_addr;
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
  Memory()
      : num_banks_(1)  // 0 is UNMAPPED
  {}

  ~Memory() {
    for (auto block : blocks_) {
      free(block);
    }
  }

  void Resolve(uint32_t logical_addr, MemoryBank **page, uint32_t *offset) {
    TableHandle handle = table_.Lookup(logical_addr);
    if (handle == UNMAPPED) {
      LOG_FATAL("Attempting to resolve unmapped address 0x%x", logical_addr);
      return;
    }

    *page = &banks_[handle];
    *offset = (logical_addr - (*page)->logical_addr) & (*page)->mirror_mask;
  }

  void Mount(uint32_t logical_start, uint32_t logical_end, uint32_t mirror_mask,
             uint8_t *physical_start) {
    MemoryBank &bank = AllocBank();
    bank.mirror_mask = ~mirror_mask;
    bank.logical_addr = logical_start;
    bank.physical_addr = physical_start;
    table_.MapRange(logical_start, logical_end, mirror_mask, bank.handle);
  }

  void Handle(uint32_t logical_start, uint32_t logical_end,
              uint32_t mirror_mask, R8Handler r8, R16Handler r16,
              R32Handler r32, R64Handler r64, W8Handler w8, W16Handler w16,
              W32Handler w32, W64Handler w64) {
    MemoryBank &bank = AllocBank();
    bank.mirror_mask = ~mirror_mask;
    bank.logical_addr = logical_start;
    bank.r8 = r8;
    bank.r16 = r16;
    bank.r32 = r32;
    bank.r64 = r64;
    bank.w8 = w8;
    bank.w16 = w16;
    bank.w32 = w32;
    bank.w64 = w64;
    table_.MapRange(logical_start, logical_end, mirror_mask, bank.handle);
  }

  void Memcpy(uint32_t logical_dest, void *ptr, uint32_t size) {
    uint8_t *src = (uint8_t *)ptr;
    uint32_t end = logical_dest + size;
    while (logical_dest < end) {
      W8(logical_dest, *src);
      logical_dest++;
      src++;
    }
  }

  void Memcpy(void *ptr, uint32_t logical_src, uint32_t size) {
    uint8_t *dest = (uint8_t *)ptr;
    uint8_t *end = dest + size;
    while (dest < end) {
      *dest = R32(logical_src);
      logical_src++;
      dest++;
    }
  }

  // static versions of the functions for ease of calling from assembly
  static uint8_t R8(Memory *memory, uint32_t addr) {
    return memory->ReadBytes<uint8_t, &MemoryBank::r8>(addr);
  }
  static uint16_t R16(Memory *memory, uint32_t addr) {
    return memory->ReadBytes<uint16_t, &MemoryBank::r16>(addr);
  }
  static uint32_t R32(Memory *memory, uint32_t addr) {
    return memory->ReadBytes<uint32_t, &MemoryBank::r32>(addr);
  }
  static uint64_t R64(Memory *memory, uint32_t addr) {
    return memory->ReadBytes<uint64_t, &MemoryBank::r64>(addr);
  }
  static void W8(Memory *memory, uint32_t addr, uint8_t value) {
    memory->WriteBytes<uint8_t, &MemoryBank::w8>(addr, value);
  }
  static void W16(Memory *memory, uint32_t addr, uint16_t value) {
    memory->WriteBytes<uint16_t, &MemoryBank::w16>(addr, value);
  }
  static void W32(Memory *memory, uint32_t addr, uint32_t value) {
    memory->WriteBytes<uint32_t, &MemoryBank::w32>(addr, value);
  }
  static void W64(Memory *memory, uint32_t addr, uint64_t value) {
    memory->WriteBytes<uint64_t, &MemoryBank::w64>(addr, value);
  }

  uint8_t R8(uint32_t addr) {
    return ReadBytes<uint8_t, &MemoryBank::r8>(addr);
  }
  uint16_t R16(uint32_t addr) {
    return ReadBytes<uint16_t, &MemoryBank::r16>(addr);
  }
  uint32_t R32(uint32_t addr) {
    return ReadBytes<uint32_t, &MemoryBank::r32>(addr);
  }
  uint64_t R64(uint32_t addr) {
    return ReadBytes<uint64_t, &MemoryBank::r64>(addr);
  }
  void W8(uint32_t addr, uint8_t value) {
    WriteBytes<uint8_t, &MemoryBank::w8>(addr, value);
  }
  void W16(uint32_t addr, uint16_t value) {
    WriteBytes<uint16_t, &MemoryBank::w16>(addr, value);
  }
  void W32(uint32_t addr, uint32_t value) {
    WriteBytes<uint32_t, &MemoryBank::w32>(addr, value);
  }
  void W64(uint32_t addr, uint64_t value) {
    WriteBytes<uint64_t, &MemoryBank::w64>(addr, value);
  }

 private:
  PageTable table_;
  int num_banks_;
  MemoryBank banks_[MAX_HANDLES];
  std::list<uint8_t *> blocks_;

  MemoryBank &AllocBank() {
    CHECK_LT(num_banks_, MAX_HANDLES);
    MemoryBank &bank = banks_[num_banks_];
    bank.handle = num_banks_++;
    return bank;
  }

  template <typename INT, std::function<INT(uint32_t)> MemoryBank::*HANDLER>
  inline INT ReadBytes(uint32_t addr) {
    TableHandle handle = table_.Lookup(addr);
    MemoryBank &bank = banks_[handle];
    uint32_t offset = (addr - bank.logical_addr) & bank.mirror_mask;
    if (bank.physical_addr) {
      return *(INT *)(bank.physical_addr + offset);
    } else if (bank.*HANDLER) {
      return (bank.*HANDLER)(offset);
    } else {
      LOG_FATAL("Attempting to read from unmapped address 0x%x", addr);
    }

    return 0;
  }

  template <typename INT,
            std::function<void(uint32_t, INT)> MemoryBank::*HANDLER>
  inline void WriteBytes(uint32_t addr, INT value) {
    TableHandle handle = table_.Lookup(addr);
    MemoryBank &bank = banks_[handle];
    uint32_t offset = (addr - bank.logical_addr) & bank.mirror_mask;
    if (bank.physical_addr) {
      *(INT *)(bank.physical_addr + offset) = value;
    } else if (bank.*HANDLER) {
      (bank.*HANDLER)(offset, value);
    } else {
      LOG_FATAL("Attempting to write to unmapped address 0x%x", addr);
    }
  }
};
}
}

#endif
