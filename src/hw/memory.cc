#include "core/memory.h"
#include "hw/memory.h"

using namespace dvm;
using namespace dvm::hw;
using namespace dvm::sys;

static inline bool PageAligned(uint32_t start, uint32_t size) {
  return (start & (PAGE_OFFSET_BITS - 1)) == 0 &&
         ((start + size) & (PAGE_OFFSET_BITS - 1)) == 0;
}

// map virtual addresses to pages
static inline int PageIndex(uint32_t virtual_addr) {
  return virtual_addr >> PAGE_OFFSET_BITS;
}

static inline uint32_t PageOffset(uint32_t virtual_addr) {
  return virtual_addr & PAGE_OFFSET_MASK;
}

// pack and unpack page entry bitstrings
static inline PageEntry PackEntry(const uint8_t *base,
                                  const MemoryRegion *region,
                                  uint32_t region_offset) {
  if (region->dynamic) {
    return region_offset | region->handle;
  }

  return reinterpret_cast<uintptr_t>(base) | region->physical_addr |
         region_offset;
}

static inline int IsStaticRegion(const PageEntry &page) {
  return !(page & PAGE_REGION_MASK);
}

static inline uint8_t *RegionPointer(const PageEntry &page) {
  return reinterpret_cast<uint8_t *>(static_cast<uintptr_t>(page));
}

static inline uint32_t RegionOffset(const PageEntry &page) {
  return page & PAGE_REGION_OFFSET_MASK;
}

static inline int RegionIndex(const PageEntry &page) {
  return page & PAGE_REGION_MASK;
}

MemoryMap::MemoryMap() : entries_(), num_entries_(0) {}

MapEntryHandle MemoryMap::Mount(RegionHandle handle, uint32_t size,
                                uint32_t virtual_addr) {
  MapEntry *entry = AllocEntry();
  entry->type = MAP_ENTRY_MOUNT;
  entry->mount.handle = handle;
  entry->mount.size = size;
  entry->mount.virtual_addr = virtual_addr;
  return entry->handle;
}

MapEntryHandle MemoryMap::Mirror(uint32_t physical_addr, uint32_t size,
                                 uint32_t virtual_addr) {
  MapEntry *entry = AllocEntry();
  entry->type = MAP_ENTRY_MIRROR;
  entry->mirror.physical_addr = physical_addr;
  entry->mirror.size = size;
  entry->mirror.virtual_addr = virtual_addr;
  return entry->handle;
}

MapEntry *MemoryMap::AllocEntry() {
  CHECK_LT(num_entries_, MAX_REGIONS);
  MapEntry *entry = &entries_[num_entries_];
  new (entry) MapEntry();
  entry->handle = num_entries_++;
  return entry;
}

// helpers for emitted assembly
uint8_t Memory::R8(Memory *memory, uint32_t addr) { return memory->R8(addr); }
uint16_t Memory::R16(Memory *memory, uint32_t addr) {
  return memory->R16(addr);
}
uint32_t Memory::R32(Memory *memory, uint32_t addr) {
  return memory->R32(addr);
}
uint64_t Memory::R64(Memory *memory, uint32_t addr) {
  return memory->R64(addr);
}
void Memory::W8(Memory *memory, uint32_t addr, uint8_t value) {
  memory->W8(addr, value);
}
void Memory::W16(Memory *memory, uint32_t addr, uint16_t value) {
  memory->W16(addr, value);
}
void Memory::W32(Memory *memory, uint32_t addr, uint32_t value) {
  memory->W32(addr, value);
}
void Memory::W64(Memory *memory, uint32_t addr, uint64_t value) {
  memory->W64(addr, value);
}

Memory::Memory()
    : shmem_(SHMEM_INVALID),
      physical_base_(nullptr),
      virtual_base_(nullptr),
      protected_base_(nullptr) {
  regions_ = new MemoryRegion[MAX_REGIONS]();
  num_regions_ = 1;  // 0 page is reserved

  pages_ = new PageEntry[NUM_PAGES]();
}

Memory::~Memory() {
  delete[] regions_;
  delete[] pages_;

  Unmap();

  DestroySharedMemory();
}

bool Memory::Init() {
  if (!CreateSharedMemory()) {
    return false;
  }

  return true;
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

void Memory::Memcpy(uint32_t virtual_dest, const void *ptr, uint32_t size) {
  uint8_t *src = (uint8_t *)ptr;
  uint32_t end = virtual_dest + size;
  while (virtual_dest < end) {
    W8(virtual_dest, *src);
    virtual_dest++;
    src++;
  }
}

void Memory::Memcpy(void *ptr, uint32_t virtual_src, uint32_t size) {
  uint8_t *dest = (uint8_t *)ptr;
  uint8_t *end = dest + size;
  while (dest < end) {
    *dest = R32(virtual_src);
    virtual_src++;
    dest++;
  }
}

void Memory::Lookup(uint32_t virtual_addr, uint8_t **ptr, MemoryRegion **region,
                    uint32_t *offset) {
  PageEntry page = pages_[PageIndex(virtual_addr)];
  uint32_t page_offset = PageOffset(virtual_addr);

  if (IsStaticRegion(page)) {
    *ptr = RegionPointer(page) + page_offset;
    *region = nullptr;
    *offset = 0;
  } else {
    *ptr = nullptr;
    *region = &regions_[RegionIndex(page)];
    *offset = RegionOffset(page) + page_offset;
  }
}

bool Memory::CreateSharedMemory() {
  // create the shared memory object to back the address space
  shmem_ = ::CreateSharedMemory("/dreavm", ADDRESS_SPACE_SIZE, ACC_READWRITE);

  if (shmem_ == SHMEM_INVALID) {
    LOG_WARNING("Failed to create shared memory object");
    return false;
  }

  return true;
}

void Memory::DestroySharedMemory() { ::DestroySharedMemory(shmem_); }

bool Memory::ReserveAddressSpace(uint8_t **base) {
  for (int i = 63; i >= 32; i--) {
    *base = reinterpret_cast<uint8_t *>(1ull << i);

    if (!ReservePages(*base, ADDRESS_SPACE_SIZE)) {
      continue;
    }

    // reservation was a success, release and expect subsequent mappings to
    // succeed
    ReleasePages(*base, ADDRESS_SPACE_SIZE);

    return true;
  }

  LOG_WARNING("Failed to reserve address space");
  return false;
}

MemoryRegion *Memory::AllocRegion() {
  CHECK_LT(num_regions_, MAX_REGIONS);
  MemoryRegion *region = &regions_[num_regions_];
  new (region) MemoryRegion();
  region->handle = num_regions_++;
  return region;
}

RegionHandle Memory::AllocRegion(uint32_t physical_addr, uint32_t size) {
  MemoryRegion *region = AllocRegion();
  region->dynamic = false;
  region->physical_addr = physical_addr;
  region->size = size;
  return region->handle;
}

RegionHandle Memory::AllocRegion(uint32_t physical_addr, uint32_t size,
                                 void *ctx, R8Handler r8, R16Handler r16,
                                 R32Handler r32, R64Handler r64, W8Handler w8,
                                 W16Handler w16, W32Handler w32,
                                 W64Handler w64) {
  MemoryRegion *region = AllocRegion();
  region->dynamic = true;
  region->physical_addr = physical_addr;
  region->size = size;
  region->ctx = ctx;
  region->r8 = r8;
  region->r16 = r16;
  region->r32 = r32;
  region->r64 = r64;
  region->w8 = w8;
  region->w16 = w16;
  region->w32 = w32;
  region->w64 = w64;
  return region->handle;
}

bool Memory::Map(const MemoryMap &map) {
  // map regions into the physical space
  if (!MapPhysicalSpace()) {
    return false;
  }

  // flatten out the memory map into a page table
  if (!CreatePageTable(map)) {
    return false;
  }

  // map page table to the virtual / protected address spaces
  if (!MapVirtualSpace()) {
    return false;
  }

  return true;
}

void Memory::Unmap() {
  UnmapPhysicalSpace();
  UnmapVirtualSpace();
}

bool Memory::GetNextContiguousRegion(uint32_t *physical_start,
                                     uint32_t *physical_end) {
  // find the next lowest region
  MemoryRegion *lowest = nullptr;

  for (int i = 0; i < num_regions_; i++) {
    MemoryRegion *region = &regions_[i];

    if (region->physical_addr >= *physical_end &&
        (!lowest || region->physical_addr <= lowest->physical_addr)) {
      lowest = region;
    }
  }

  // no more regions
  if (!lowest) {
    return false;
  }

  // find the extent of the contiguous region starting at lowest
  MemoryRegion *highest = lowest;

  for (int i = 0; i < num_regions_; i++) {
    MemoryRegion *region = &regions_[i];

    uint32_t highest_end = highest->physical_addr + highest->size;
    uint32_t region_end = region->physical_addr + region->size;

    if (region->physical_addr <= highest_end && region_end >= highest_end) {
      highest = region;
    }
  }

  *physical_start = lowest->physical_addr;
  *physical_end = highest->physical_addr + highest->size;

  return true;
}

bool Memory::MapPhysicalSpace() {
  UnmapPhysicalSpace();

  ReserveAddressSpace(&physical_base_);

  // map flattened physical regions into physical address space. it's important
  // to flatten them for two reasons:
  // 1.) to have as large of mappings as possible. Windows won't allow you to
  //     map < 64k chunks
  // 2.) shared memory mappings can't overlap on Windows
  uint32_t physical_start = 0;
  uint32_t physical_end = 0;

  while (GetNextContiguousRegion(&physical_start, &physical_end)) {
    uint32_t physical_size = physical_end - physical_start;

    if (!MapSharedMemory(shmem_, physical_start,
                         physical_base_ + physical_start, physical_size,
                         ACC_READWRITE)) {
      return false;
    }
  }

  return true;
}

void Memory::UnmapPhysicalSpace() {
  if (!physical_base_) {
    return;
  }

  uint32_t physical_start = 0;
  uint32_t physical_end = 0;

  while (GetNextContiguousRegion(&physical_start, &physical_end)) {
    uint32_t physical_size = physical_end - physical_start;

    UnmapSharedMemory(shmem_, physical_base_ + physical_start, physical_size);
  }
}

int Memory::GetNumAdjacentPhysicalPages(int first_page_index) {
  int i;

  for (i = first_page_index; i < NUM_PAGES - 1; i++) {
    PageEntry page = pages_[i];
    PageEntry next_page = pages_[i + 1];

    uint32_t physical_addr = GetPhysicalAddress(page);
    uint32_t next_physical_addr = GetPhysicalAddress(next_page);

    if ((next_physical_addr - physical_addr) != PAGE_BLKSIZE) {
      break;
    }
  }

  return (i + 1) - first_page_index;
}

uint32_t Memory::GetPhysicalAddress(const PageEntry &page) {
  if (IsStaticRegion(page)) {
    return static_cast<uint32_t>(RegionPointer(page) - physical_base_);
  }

  MemoryRegion &region = regions_[RegionIndex(page)];
  return region.physical_addr + RegionOffset(page);
}

// Iterate regions in the supplied memory map in the other added, flattening
// them out into a virtual page table.
bool Memory::CreatePageTable(const MemoryMap &map) {
  for (int i = 0, n = map.num_entries(); i < n; i++) {
    const MapEntry *entry = map.entry(i);

    switch (entry->type) {
      case MAP_ENTRY_MOUNT: {
        if (!PageAligned(entry->mount.virtual_addr,
                         entry->mount.virtual_addr + entry->mount.size)) {
          return false;
        }

        MemoryRegion *region = &regions_[entry->mount.handle];
        int first_virtual_page = PageIndex(entry->mount.virtual_addr);
        int num_pages = entry->mount.size >> PAGE_OFFSET_BITS;

        // create an entry in the page table for each page the region occupies
        for (int i = 0; i < num_pages; i++) {
          uint32_t region_offset = i * PAGE_BLKSIZE;

          pages_[first_virtual_page + i] =
              PackEntry(physical_base_, region, region_offset);
        }
      } break;

      case MAP_ENTRY_MIRROR: {
        if (!PageAligned(entry->mirror.virtual_addr,
                         entry->mirror.virtual_addr + entry->mirror.size) ||
            !PageAligned(entry->mirror.physical_addr,
                         entry->mirror.physical_addr + entry->mirror.size)) {
          return false;
        }

        int first_virtual_page = PageIndex(entry->mirror.virtual_addr);
        int first_physical_page = PageIndex(entry->mirror.physical_addr);
        int num_pages = entry->mirror.size >> PAGE_OFFSET_BITS;

        // copy the page entries for the requested physical range into the new
        // virtual address range
        for (int i = 0; i < num_pages; i++) {
          pages_[first_virtual_page + i] = pages_[first_physical_page + i];
        }
      } break;
    }
  }

  return true;
}

bool Memory::MapVirtualSpace() {
  UnmapVirtualSpace();

  // map page table to virtual and protected address spaces
  auto map_virtual = [&](uint8_t **base) {
    ReserveAddressSpace(base);

    for (int page_index = 0; page_index < NUM_PAGES;) {
      PageEntry page = pages_[page_index];

      if (!page) {
        page_index++;
        continue;
      }

      // batch map virtual pages which map to adjacent physical pages, mmap is
      // fairly slow
      int num_pages = GetNumAdjacentPhysicalPages(page_index);
      uint32_t size = num_pages * PAGE_BLKSIZE;

      // mmap the range in the physical address space to the shared memory
      // object
      uint32_t virtual_addr = page_index * PAGE_BLKSIZE;
      uint32_t physical_addr = GetPhysicalAddress(page);

      if (!MapSharedMemory(shmem_, physical_addr, *base + virtual_addr, size,
                           ACC_READWRITE)) {
        return false;
      }

      page_index += num_pages;
    }

    return true;
  };

  if (!map_virtual(&virtual_base_)) {
    return false;
  }

  if (!map_virtual(&protected_base_)) {
    return false;
  }

  // protect dynamic regions in protected address space
  for (int page_index = 0; page_index < NUM_PAGES; page_index++) {
    PageEntry page = pages_[page_index];

    if (!page || IsStaticRegion(page)) {
      continue;
    }

    uint32_t virtual_addr = page_index * PAGE_BLKSIZE;
    ProtectPages(protected_base_ + virtual_addr, PAGE_BLKSIZE, ACC_NONE);
  }

  return true;
}

void Memory::UnmapVirtualSpace() {
  auto unmap_virtual = [&](uint8_t *base) {
    if (!base) {
      return;
    }

    for (int page_index = 0; page_index < NUM_PAGES;) {
      PageEntry page = pages_[page_index];

      if (!page) {
        page_index++;
        continue;
      }

      uint32_t virtual_addr = page_index * PAGE_BLKSIZE;

      int num_pages = GetNumAdjacentPhysicalPages(page_index);
      uint32_t size = num_pages * PAGE_BLKSIZE;

      CHECK(UnmapSharedMemory(shmem_, base + virtual_addr, size));

      page_index += num_pages;
    }
  };

  unmap_virtual(virtual_base_);
  unmap_virtual(protected_base_);
}

template <typename INT, INT (*MemoryRegion::*HANDLER)(void *, uint32_t)>
inline INT Memory::ReadBytes(uint32_t addr) {
  PageEntry page = pages_[PageIndex(addr)];
  uint32_t page_offset = PageOffset(addr);
  DCHECK(page);

  if (IsStaticRegion(page)) {
    return dvm::load<INT>(RegionPointer(page) + page_offset);
  }

  MemoryRegion &region = regions_[RegionIndex(page)];
  if (!(region.*HANDLER)) {
    LOG_WARNING("Unmapped read at 0x%08x", addr);
    return 0;
  }

  uint32_t region_offset = RegionOffset(page);
  return (region.*HANDLER)(region.ctx, region_offset + page_offset);
}

template <typename INT, void (*MemoryRegion::*HANDLER)(void *, uint32_t, INT)>
inline void Memory::WriteBytes(uint32_t addr, INT value) {
  PageEntry page = pages_[PageIndex(addr)];
  uint32_t page_offset = PageOffset(addr);
  DCHECK(page);

  if (IsStaticRegion(page)) {
    dvm::store(RegionPointer(page) + page_offset, value);
    return;
  }

  MemoryRegion &region = regions_[RegionIndex(page)];
  if (!(region.*HANDLER)) {
    LOG_WARNING("Unmapped write at 0x%08x", addr);
    return;
  }

  uint32_t region_offset = RegionOffset(page);
  (region.*HANDLER)(region.ctx, region_offset + page_offset, value);
}
