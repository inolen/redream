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

Memory::Memory() : physical_base_(nullptr), virtual_base_(nullptr) {
  exc_handler_ =
      ExceptionHandler::instance().AddHandler(this, &Memory::HandleException);
}

Memory::~Memory() {
  UnmapAddressSpace();
  DestroyAddressSpace();

  ExceptionHandler::instance().RemoveHandler(exc_handler_);
}

bool Memory::Init(const MemoryMap &map) {
  map_ = map;

  if (!CreateAddressSpace()) {
    return false;
  }

  return true;
}

void Memory::Lookup(uint32_t logical_addr, MemoryRegion **region,
                    uint32_t *offset) {
  map_.Lookup(logical_addr, region, offset);
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

WatchHandle Memory::AddSingleWriteWatch(void *ptr, size_t size,
                                        WatchHandler handler, void *ctx,
                                        void *data) {
  // page align the range to be watched
  size_t page_size = GetPageSize();
  ptr = reinterpret_cast<void *>(dreavm::align(
      reinterpret_cast<uintptr_t>(ptr), static_cast<uintptr_t>(page_size)));
  size = dreavm::align(size, page_size);

  // disable writing to the pages
  CHECK(ProtectPages(ptr, size, ACC_READONLY));

  uintptr_t start = reinterpret_cast<uintptr_t>(ptr);
  uintptr_t end = start + size - 1;
  WatchHandle handle = watches_.Insert(
      start, end, Watch{WATCH_SINGLE_WRITE, handler, ctx, data, ptr, size});

  return handle;
}

void Memory::RemoveWatch(WatchHandle handle) { watches_.Remove(handle); }

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

bool Memory::HandleException(void *ctx, Exception &ex) {
  Memory *self = reinterpret_cast<Memory *>(ctx);

  auto range_it = self->watches_.intersect(ex.fault_addr, ex.fault_addr);
  auto it = range_it.first;
  auto end = range_it.second;

  while (it != end) {
    WatchTree::node_type *node = *(it++);
    Watch &watch = node->value;

    watch.handler(watch.ctx, ex, watch.data);

    if (watch.type == WATCH_SINGLE_WRITE) {
      // restore page permissions
      CHECK(ProtectPages(watch.ptr, watch.size, ACC_READWRITE));

      self->watches_.Remove(node);
    }
  }

  return range_it.first != range_it.second;
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

    if (!MapAddressSpace()) {
      UnmapAddressSpace();
      physical_base_ = nullptr;
      virtual_base_ = nullptr;
      continue;
    }

    // success
    break;
  }

  if (!physical_base_ || !virtual_base_) {
    LOG_WARNING("Failed to reserve address space");
    return false;
  }

  return true;
}

void Memory::DestroyAddressSpace() { CHECK(DestroySharedMemory(shmem_)); }

bool Memory::MapAddressSpace() {
  int i = 0;
  int j = 0;
  int n = map_.num_pages();

  for (; i < n;) {
    MemoryRegion *region = map_.page(i);

    // ignore empty pages
    if (!region) {
      i++;
      continue;
    }

    // work with adjacent regions at the same time, mmap is fairly slow
    for (j = i + 1; j < n; j++) {
      MemoryRegion *next = map_.page(j);

      if (next != region) {
        break;
      }
    }

    uint32_t base = i * MAX_PAGE_SIZE;
    uint32_t size = (j - i) * MAX_PAGE_SIZE;

    // set the region's physical location in memory
    region->data = physical_base_ + region->logical_addr;

    // mmap the shared memory object to the physical and virtual regions
    if (!MapSharedMemory(shmem_, physical_base_ + base, region->logical_addr,
                         size, ACC_READWRITE)) {
      return false;
    }

    if (!MapSharedMemory(shmem_, virtual_base_ + base, region->logical_addr,
                         size, ACC_READWRITE)) {
      return false;
    }

    // if this address represents a dynamic handler, mprotect the pages so
    // accesses to them will raise a segfault that can be handled to recompile
    // the block accessing them
    if (region->dynamic &&
        !ProtectPages(virtual_base_ + base, size, ACC_NONE)) {
      return false;
    }

    // update mapped status for all pages
    for (int k = i; k < j; k++) {
      MemoryRegion *next = map_.page(k);
      next->mapped = true;
    }

    i = j;
  }

  return true;
}

void Memory::UnmapAddressSpace() {
  int i = 0;
  int j = 0;
  int n = map_.num_pages();

  for (; i < n;) {
    MemoryRegion *region = map_.page(i);

    if (!region || !region->mapped) {
      i++;
      continue;
    }

    // work with adjacent regions at the same time, mmap is fairly slow
    for (j = i + 1; j < n; j++) {
      const MemoryRegion *next = map_.page(j);

      if (next != region) {
        break;
      }
    }

    uint32_t base = i * MAX_PAGE_SIZE;
    uint32_t size = (j - i) * MAX_PAGE_SIZE;

    // restore permissions on pages for dynamic handlers
    if (region->dynamic) {
      CHECK(ProtectPages(virtual_base_ + base, size, ACC_READWRITE));
    }

    // unmap the shared memory from both regions
    CHECK(UnmapSharedMemory(shmem_, physical_base_ + base, size));
    CHECK(UnmapSharedMemory(shmem_, virtual_base_ + base, size));

    // update mapped status for all pages
    for (int k = i; k < j; k++) {
      MemoryRegion *next = map_.page(k);
      next->mapped = false;
    }

    i = j;
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
