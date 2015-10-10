#include "core/core.h"
#include "hw/memory.h"
#include "sys/sigsegv_handler.h"

using namespace dreavm::hw;
using namespace dreavm::sys;

// from a hardware perspective, the mirror mask parameter describes the
// address bits which are ignored for the specified range.
//
// from our perspective however, each permutation of these bits describes
// a mirror of the address range.
//
// for example, on the dreamcast bits 29-31 are ignored for each address.
// this means that 0x00040000 is also available at 0x20040000, 0x40040000,
// 0x60040000, 0x80040000, 0xa0040000, 0xc0040000 and 0xe0040000.
struct MirrorIterator {
  MirrorIterator(uint32_t addr, uint32_t mask)
      : base(addr & ~mask),
        mask(mask),
        step(1 << dreavm::ctz(mask)),
        i(0),
        addr(base),
        first(true) {}

  uint32_t base, mask, step;
  uint32_t i, addr;
  bool first;
};

static bool NextMirror(MirrorIterator *it) {
  // first iteration just returns base
  if (it->first) {
    it->first = false;
    return true;
  }

  // stop once mask is completely set
  if ((it->addr & it->mask) == it->mask) {
    return false;
  }

  // step to the next permutation
  it->i += it->step;

  // if the new value carries over into a masked off bit, skip it
  uint32_t carry;
  do {
    carry = it->i & ~it->mask;
    it->i += carry;
  } while (carry);

  // merge with the base
  it->addr = it->base | it->i;

  return true;
}

PageTable::PageTable() {
  table_ = new TableHandle[MAX_ENTRIES];

  for (int i = 0; i < MAX_ENTRIES; i++) {
    table_[i] = UNMAPPED;
  }
}

PageTable::~PageTable() { delete[] table_; }

inline TableHandle PageTable::Lookup(uint32_t addr) {
  return table_[addr >> OFFSET_BITS];
}

void PageTable::MapRange(uint32_t addr, uint32_t size, TableHandle handle) {
  uint32_t start = addr;
  uint32_t end = start + size - 1;

  // ensure start and end are page aligned
  CHECK_EQ(start & (OFFSET_BITS - 1), 0u);
  CHECK_EQ((end + 1) & (OFFSET_BITS - 1), 0u);
  CHECK_LT(start, end);

  int l1_start = start >> OFFSET_BITS;
  int l1_end = end >> OFFSET_BITS;

  for (int i = l1_start; i <= l1_end; i++) {
    table_[i] = handle;
  }
}

uint8_t Memory::R8(Memory *memory, uint32_t addr) {
  return memory->ReadBytes<uint8_t, &MemoryBank::r8>(addr);
}

uint16_t Memory::R16(Memory *memory, uint32_t addr) {
  return memory->ReadBytes<uint16_t, &MemoryBank::r16>(addr);
}

uint32_t Memory::R32(Memory *memory, uint32_t addr) {
  return memory->ReadBytes<uint32_t, &MemoryBank::r32>(addr);
}

uint64_t Memory::R64(Memory *memory, uint32_t addr) {
  return memory->ReadBytes<uint64_t, &MemoryBank::r64>(addr);
}

void Memory::W8(Memory *memory, uint32_t addr, uint8_t value) {
  memory->WriteBytes<uint8_t, &MemoryBank::w8>(addr, value);
}

void Memory::W16(Memory *memory, uint32_t addr, uint16_t value) {
  memory->WriteBytes<uint16_t, &MemoryBank::w16>(addr, value);
}

void Memory::W32(Memory *memory, uint32_t addr, uint32_t value) {
  memory->WriteBytes<uint32_t, &MemoryBank::w32>(addr, value);
}

void Memory::W64(Memory *memory, uint32_t addr, uint64_t value) {
  memory->WriteBytes<uint64_t, &MemoryBank::w64>(addr, value);
}

Memory::Memory()
    : physical_base_(nullptr),
      virtual_base_(nullptr),
      virtual_handler_(nullptr),
      virtual_handler_ctx_(nullptr),
      num_banks_(1),  // 0 is UNMAPPED
      banks_() {}

Memory::~Memory() { DestroyAddressSpace(); }

bool Memory::Init() {
  if (!CreateAddressSpace()) {
    return false;
  }

  SIGSEGVHandler::instance()->AddAccessFaultWatch(
      virtual_base_, ADDRESS_SPACE_SIZE, &Memory::HandleAccessFault, this,
      nullptr);

  return true;
}

bool Memory::Resolve(uint32_t logical_addr, MemoryBank **out_bank,
                     uint32_t *out_offset) {
  TableHandle handle = table_.Lookup(logical_addr);
  MemoryBank &bank = banks_[handle];
  *out_bank = &bank;
  *out_offset = (logical_addr - bank.logical_addr) & bank.addr_mask;
  return handle != UNMAPPED;
}

uint8_t *Memory::Alloc(uint32_t logical_addr, uint32_t size,
                       uint32_t mirror_mask) {
  // allocate bank for the range. this isn't mapped in the page table, but used
  // for cleaning up shared memory mappings
  MemoryBank &bank = AllocBank();
  bank.addr_mask = ~mirror_mask;
  bank.logical_addr = logical_addr;
  bank.size = size;

  // map shared memory for each mirrored range
  MirrorIterator it(logical_addr, mirror_mask);
  while (NextMirror(&it)) {
    CHECK(MapSharedMemory(shm_, physical_base_ + it.addr, logical_addr, size,
                          ACC_READWRITE));

    CHECK(MapSharedMemory(shm_, virtual_base_ + it.addr, logical_addr, size,
                          ACC_READWRITE));
  }

  return physical_base_ + logical_addr;
}

void Memory::Handle(uint32_t logical_addr, uint32_t size, uint32_t mirror_mask,
                    void *ctx, R8Handler r8, R16Handler r16, R32Handler r32,
                    R64Handler r64, W8Handler w8, W16Handler w16,
                    W32Handler w32, W64Handler w64) {
  // allocate bank for the handlers
  MemoryBank &bank = AllocBank();
  bank.addr_mask = ~mirror_mask;
  bank.logical_addr = logical_addr;
  bank.size = size;
  bank.ctx = ctx;
  bank.r8 = r8;
  bank.r16 = r16;
  bank.r32 = r32;
  bank.r64 = r64;
  bank.w8 = w8;
  bank.w16 = w16;
  bank.w32 = w32;
  bank.w64 = w64;

  // map each mirror in the page table and protect them so interrupts are
  // triggered when reading or writing from the virtual address space
  MirrorIterator it(logical_addr, mirror_mask);
  while (NextMirror(&it)) {
    table_.MapRange(it.addr, size, bank.handle);

    CHECK(ProtectPages(virtual_base_ + it.addr, size, ACC_NONE));
  }
}

void Memory::Memcpy(uint32_t logical_dest, void *ptr, uint32_t size) {
  uint8_t *src = (uint8_t *)ptr;
  uint32_t end = logical_dest + size;
  while (logical_dest < end) {
    W8(logical_dest, *src);
    logical_dest++;
    src++;
  }
}

void Memory::Memcpy(void *ptr, uint32_t logical_src, uint32_t size) {
  uint8_t *dest = (uint8_t *)ptr;
  uint8_t *end = dest + size;
  while (dest < end) {
    *dest = R32(logical_src);
    logical_src++;
    dest++;
  }
}

uint8_t Memory::R8(uint32_t addr) {
  return ReadBytes<uint8_t, &MemoryBank::r8>(addr);
}

uint16_t Memory::R16(uint32_t addr) {
  return ReadBytes<uint16_t, &MemoryBank::r16>(addr);
}

uint32_t Memory::R32(uint32_t addr) {
  return ReadBytes<uint32_t, &MemoryBank::r32>(addr);
}

uint64_t Memory::R64(uint32_t addr) {
  return ReadBytes<uint64_t, &MemoryBank::r64>(addr);
}

void Memory::W8(uint32_t addr, uint8_t value) {
  WriteBytes<uint8_t, &MemoryBank::w8>(addr, value);
}

void Memory::W16(uint32_t addr, uint16_t value) {
  WriteBytes<uint16_t, &MemoryBank::w16>(addr, value);
}

void Memory::W32(uint32_t addr, uint32_t value) {
  WriteBytes<uint32_t, &MemoryBank::w32>(addr, value);
}

void Memory::W64(uint32_t addr, uint64_t value) {
  WriteBytes<uint64_t, &MemoryBank::w64>(addr, value);
}

void Memory::HandleAccessFault(void *ctx, void *data, uintptr_t rip,
                               uintptr_t fault_addr) {
  Memory *memory = reinterpret_cast<Memory *>(ctx);

  CHECK_NOTNULL(memory->virtual_handler_);
  CHECK_NOTNULL(memory->virtual_handler_ctx_);

  memory->virtual_handler_(memory->virtual_handler_ctx_, rip, fault_addr);
}

bool Memory::CreateAddressSpace() {
  // two 32-bit address spaces are needed - one for direct access and one that
  // will trigger interrupts for virtual handlers
  for (int i = 62; i >= 32; i--) {
    uint8_t *base = reinterpret_cast<uint8_t *>(1ull << i);

    // try and reserve both spaces
    void *physical_base = base;
    if (!ReservePages(physical_base, ADDRESS_SPACE_SIZE)) {
      continue;
    }

    void *virtual_base = base + ADDRESS_SPACE_SIZE;
    if (!ReservePages(virtual_base, ADDRESS_SPACE_SIZE)) {
      ReleasePages(physical_base, ADDRESS_SPACE_SIZE);
      continue;
    }

    // successfully reserved both address spaces, release both now and pray
    // that subsequent calls to Alloc / Handle succeed for these ranges. note,
    // this is terrible, perhaps all allocations and handlers should be passed
    // as some sort of address map object to Memory::Init
    ReleasePages(physical_base, ADDRESS_SPACE_SIZE);
    ReleasePages(virtual_base, ADDRESS_SPACE_SIZE);

    physical_base_ = reinterpret_cast<uint8_t *>(physical_base);
    virtual_base_ = reinterpret_cast<uint8_t *>(virtual_base);
    break;
  }

  if (!physical_base_ || !virtual_base_) {
    LOG_WARNING("Failed to reserve address space");
    return false;
  }

  // create the shared memory object to back the address space
  shm_ = CreateSharedMemory("/dreavm", ADDRESS_SPACE_SIZE, ACC_READWRITE);

  if (shm_ == SHMEM_INVALID) {
    LOG_WARNING("Failed to create shared memory object");
    return false;
  }

  return true;
}

void Memory::DestroyAddressSpace() {
  // skip 0, it's UNMAPPED
  for (int i = 1; i < num_banks_; i++) {
    MemoryBank &bank = banks_[i];

    if (bank.ctx) {
      continue;
    }

    // unmap shared memory
    uint32_t mirror_mask = ~bank.addr_mask;
    uint32_t logical_addr = bank.logical_addr;
    uint32_t size = bank.size;

    MirrorIterator it(logical_addr, mirror_mask);
    while (NextMirror(&it)) {
      CHECK(UnmapSharedMemory(shm_, physical_base_ + it.addr, size));
      CHECK(UnmapSharedMemory(shm_, virtual_base_ + it.addr, size));
    }
  }

  CHECK(DestroySharedMemory(shm_));
}

MemoryBank &Memory::AllocBank() {
  CHECK_LT(num_banks_, MAX_HANDLES);
  MemoryBank &bank = banks_[num_banks_];
  new (&bank) MemoryBank();
  bank.handle = num_banks_++;
  return bank;
}

template <typename INT, INT (*MemoryBank::*HANDLER)(void *, uint32_t)>
inline INT Memory::ReadBytes(uint32_t addr) {
  TableHandle handle = table_.Lookup(addr);

  if (handle == UNMAPPED) {
    return *(INT *)(physical_base_ + addr);
  }

  MemoryBank &bank = banks_[handle];
  uint32_t offset = (addr - bank.logical_addr) & bank.addr_mask;
  return (bank.*HANDLER)(bank.ctx, offset);
}

template <typename INT, void (*MemoryBank::*HANDLER)(void *, uint32_t, INT)>
inline void Memory::WriteBytes(uint32_t addr, INT value) {
  TableHandle handle = table_.Lookup(addr);

  if (handle == UNMAPPED) {
    *(INT *)(physical_base_ + addr) = value;
    return;
  }

  MemoryBank &bank = banks_[handle];
  uint32_t offset = (addr - bank.logical_addr) & bank.addr_mask;
  (bank.*HANDLER)(bank.ctx, offset, value);
}
