#include "core/core.h"
#include "hw/memory.h"

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

MemoryMap::MemoryMap()
    : pages_(),
      regions_(),
      num_regions_(1)  // 0 is UNMAPPED
{}

void MemoryMap::Lookup(uint32_t logical_addr, MemoryRegion **region,
                       uint32_t *offset) {
  RegionHandle handle = pages_[logical_addr >> OFFSET_BITS];
  CHECK_NE(handle, UNMAPPED);
  *region = &regions_[handle];
  *offset = (logical_addr - (*region)->logical_addr) & (*region)->addr_mask;
}

void MemoryMap::Mirror(uint32_t logical_addr, uint32_t size,
                       uint32_t mirror_mask) {
  // allocate region
  MemoryRegion *region = AllocRegion();
  region->addr_mask = ~mirror_mask;
  region->logical_addr = logical_addr;
  region->size = size;
  region->dynamic = false;

  // map the region into the page table
  MirrorIterator it(logical_addr, mirror_mask);
  while (NextMirror(&it)) {
    MapRegion(it.addr, size, region);
  }
}

void MemoryMap::Handle(uint32_t logical_addr, uint32_t size,
                       uint32_t mirror_mask, void *ctx, R8Handler r8,
                       R16Handler r16, R32Handler r32, R64Handler r64,
                       W8Handler w8, W16Handler w16, W32Handler w32,
                       W64Handler w64) {
  // allocate region
  MemoryRegion *region = AllocRegion();
  region->addr_mask = ~mirror_mask;
  region->logical_addr = logical_addr;
  region->size = size;
  region->dynamic = true;
  region->ctx = ctx;
  region->r8 = r8;
  region->r16 = r16;
  region->r32 = r32;
  region->r64 = r64;
  region->w8 = w8;
  region->w16 = w16;
  region->w32 = w32;
  region->w64 = w64;

  // map the region into the page table
  MirrorIterator it(logical_addr, mirror_mask);
  while (NextMirror(&it)) {
    MapRegion(it.addr, size, region);
  }
}

MemoryRegion *MemoryMap::AllocRegion() {
  CHECK_LT(num_regions_, MAX_REGIONS);
  MemoryRegion *region = &regions_[num_regions_++];
  new (region) MemoryRegion();
  return region;
}

void MemoryMap::MapRegion(uint32_t addr, uint32_t size, MemoryRegion *region) {
  uint32_t start = addr;
  uint32_t end = start + size - 1;
  RegionHandle handle = static_cast<RegionHandle>(region - regions_);

  // ensure start and end are page aligned
  CHECK_EQ(start & (OFFSET_BITS - 1), 0u);
  CHECK_EQ((end + 1) & (OFFSET_BITS - 1), 0u);
  CHECK_LT(start, end);

  int l1_start = start >> OFFSET_BITS;
  int l1_end = end >> OFFSET_BITS;

  for (int i = l1_start; i <= l1_end; i++) {
    pages_[i] = handle;
  }
}

uint8_t Memory::R8(Memory *memory, uint32_t addr) {
  return memory->ReadBytes<uint8_t, &MemoryRegion::r8>(addr);
}

uint16_t Memory::R16(Memory *memory, uint32_t addr) {
  return memory->ReadBytes<uint16_t, &MemoryRegion::r16>(addr);
}

uint32_t Memory::R32(Memory *memory, uint32_t addr) {
  return memory->ReadBytes<uint32_t, &MemoryRegion::r32>(addr);
}

uint64_t Memory::R64(Memory *memory, uint32_t addr) {
  return memory->ReadBytes<uint64_t, &MemoryRegion::r64>(addr);
}

void Memory::W8(Memory *memory, uint32_t addr, uint8_t value) {
  memory->WriteBytes<uint8_t, &MemoryRegion::w8>(addr, value);
}

void Memory::W16(Memory *memory, uint32_t addr, uint16_t value) {
  memory->WriteBytes<uint16_t, &MemoryRegion::w16>(addr, value);
}

void Memory::W32(Memory *memory, uint32_t addr, uint32_t value) {
  memory->WriteBytes<uint32_t, &MemoryRegion::w32>(addr, value);
}

void Memory::W64(Memory *memory, uint32_t addr, uint64_t value) {
  memory->WriteBytes<uint64_t, &MemoryRegion::w64>(addr, value);
}

Memory::Memory()
    : shmem_(SHMEM_INVALID), physical_base_(nullptr), virtual_base_(nullptr) {}

Memory::~Memory() {
  UnmapAddressSpace();
  DestroyAddressSpace();
}

bool Memory::Init(const MemoryMap &map) {
  map_ = map;

  return CreateAddressSpace();
}

void Memory::Lookup(uint32_t logical_addr, MemoryRegion **region,
                    uint32_t *offset) {
  map_.Lookup(logical_addr, region, offset);
}

void Memory::Memcpy(uint32_t logical_dest, const void *ptr, uint32_t size) {
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
  return ReadBytes<uint8_t, &MemoryRegion::r8>(addr);
}

uint16_t Memory::R16(uint32_t addr) {
  return ReadBytes<uint16_t, &MemoryRegion::r16>(addr);
}

uint32_t Memory::R32(uint32_t addr) {
  return ReadBytes<uint32_t, &MemoryRegion::r32>(addr);
}

uint64_t Memory::R64(uint32_t addr) {
  return ReadBytes<uint64_t, &MemoryRegion::r64>(addr);
}

void Memory::W8(uint32_t addr, uint8_t value) {
  WriteBytes<uint8_t, &MemoryRegion::w8>(addr, value);
}

void Memory::W16(uint32_t addr, uint16_t value) {
  WriteBytes<uint16_t, &MemoryRegion::w16>(addr, value);
}

void Memory::W32(uint32_t addr, uint32_t value) {
  WriteBytes<uint32_t, &MemoryRegion::w32>(addr, value);
}

void Memory::W64(uint32_t addr, uint64_t value) {
  WriteBytes<uint64_t, &MemoryRegion::w64>(addr, value);
}

bool Memory::CreateAddressSpace() {
  // create the shared memory object to back the address space
  shmem_ = CreateSharedMemory("/dreavm", ADDRESS_SPACE_SIZE, ACC_READWRITE);

  if (shmem_ == SHMEM_INVALID) {
    LOG_WARNING("Failed to create shared memory object");
    return false;
  }

  // two 32-bit address spaces are needed - one for direct access and one that
  // will trigger interrupts for virtual handlers
  for (int i = 62; i >= 32; i--) {
    uint8_t *base = reinterpret_cast<uint8_t *>(1ull << i);

    // try and reserve both spaces
    physical_base_ = base;
    if (!ReservePages(physical_base_, ADDRESS_SPACE_SIZE)) {
      physical_base_ = nullptr;
      continue;
    }

    virtual_base_ = base + ADDRESS_SPACE_SIZE;
    if (!ReservePages(virtual_base_, ADDRESS_SPACE_SIZE)) {
      ReleasePages(physical_base_, ADDRESS_SPACE_SIZE);
      physical_base_ = nullptr;
      virtual_base_ = nullptr;
      continue;
    }

    // successfully reserved both spaces, release the reservations and try to
    // map the shared memory object in this space
    ReleasePages(physical_base_, ADDRESS_SPACE_SIZE);
    ReleasePages(virtual_base_, ADDRESS_SPACE_SIZE);

    // assume we will be able to map into physical_base_ and virtual_base_ now
    // NOTE: there is the tiny possibility that something, somehow could
    // allocate into these ranges between releasing and mapping
    MapAddressSpace();

    // success
    break;
  }

  if (!physical_base_ || !virtual_base_) {
    LOG_WARNING("Failed to reserve address space");
    return false;
  }

  return true;
}

void Memory::DestroyAddressSpace() { DestroySharedMemory(shmem_); }

void Memory::MapAddressSpace() {
  for (int i = 0, n = map_.num_pages(); i < n; i++) {
    RegionHandle handle = map_.page(i);
    MemoryRegion *region = map_.region(handle);

    // ignore empty pages or regions that've already been mapped
    if (handle == UNMAPPED || region->mapped) {
      continue;
    }

    // mmap the shared memory object to the physical and virtual regions
    CHECK(MapSharedMemory(shmem_, physical_base_ + region->logical_addr, region->logical_addr,
                          region->size, ACC_READWRITE));

    // if this address represents a dynamic handler, mprotect the pages so
    // accesses to them will raise a segfault that can be handled to recompile
    // the block accessing them
    CHECK(MapSharedMemory(shmem_, virtual_base_ + region->logical_addr, region->logical_addr,
                          region->size, region->dynamic ? ACC_NONE : ACC_READWRITE));

    // set the region's physical location in memory
    region->data = physical_base_ + region->logical_addr;

    // avoid double mapping
    region->mapped = true;
  }
}

void Memory::UnmapAddressSpace() {
  for (int i = 0, n = map_.num_pages(); i < n; i++) {
    RegionHandle handle = map_.page(i);
    MemoryRegion *region = map_.region(handle);

    if (handle == UNMAPPED || !region->mapped) {
      continue;
    }

    // unmap the shared memory from both regions
    CHECK(UnmapSharedMemory(shmem_, physical_base_ + region->logical_addr, region->size));
    CHECK(UnmapSharedMemory(shmem_, virtual_base_ + region->logical_addr, region->size));

    // can be mapped again
    region->mapped = false;
  }
}

template <typename INT, INT (*MemoryRegion::*HANDLER)(void *, uint32_t)>
inline INT Memory::ReadBytes(uint32_t addr) {
  MemoryRegion *region = nullptr;
  uint32_t offset = 0;
  map_.Lookup(addr, &region, &offset);

  if (!region->dynamic) {
    return *(INT *)(region->data + offset);
  }

  return (region->*HANDLER)(region->ctx, offset);
}

template <typename INT, void (*MemoryRegion::*HANDLER)(void *, uint32_t, INT)>
inline void Memory::WriteBytes(uint32_t addr, INT value) {
  MemoryRegion *region = nullptr;
  uint32_t offset = 0;
  map_.Lookup(addr, &region, &offset);

  if (!region->dynamic) {
    *(INT *)(region->data + offset) = value;
    return;
  }

  (region->*HANDLER)(region->ctx, offset, value);
}
