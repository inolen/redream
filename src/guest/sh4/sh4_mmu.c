#include "guest/sh4/sh4.h"

#if 0
#define LOG_MMU LOG_INFO
#else
#define LOG_MMU(...)
#endif

#define TLB_INDEX(addr) (((addr) >> 8) & 0x3f)

/*#define PAGE_SIZE(entry) (((entry)->lo.SZ1 << 1) | (entry)->lo.SZ0)*/

enum {
  PAGE_SIZE_1KB,
  PAGE_SIZE_4KB,
  PAGE_SIZE_64KB,
  PAGE_SIZE_1MB,
};

static void sh4_mmu_utlb_sync(struct sh4 *sh4, struct sh4_tlb_entry *entry) {
  int n = (int)(entry - sh4->utlb);

  /* check if entry maps to sq region [0xe0000000, 0xe3ffffff] */
  if ((entry->hi.VPN & (0xfc000000 >> 10)) == (0xe0000000 >> 10)) {
    /* assume page size is 1MB
       FIXME support all page sizes */
    uint32_t vpn = entry->hi.VPN >> 10;
    uint32_t ppn = entry->lo.PPN << 10;

    sh4->utlb_sq_map[vpn & 0x3f] = ppn;

    LOG_INFO("sh4_mmu_utlb_sync sq map (%d) 0x%x -> 0x%x", n, vpn, ppn);
  } else {
    LOG_WARNING("sh4_mmu_utlb_sync memory mapping not supported");
  }
}

static void sh4_mmu_translate(struct sh4 *sh4, uint32_t addr) {}

void sh4_mmu_ltlb(struct sh4 *sh4) {
  uint32_t n = sh4->MMUCR->URC;
  struct sh4_tlb_entry *entry = &sh4->utlb[n];
  entry->lo = *sh4->PTEL;
  entry->hi = *sh4->PTEH;

  sh4_mmu_utlb_sync(sh4, entry);
}

uint32_t sh4_mmu_itlb_read(struct sh4 *sh4, uint32_t addr, uint32_t mask) {
  if (addr < 0x01000000) {
    LOG_MMU("sh4_mmu_itlb_read address array %08x", addr);
  } else {
    LOG_MMU("sh4_mmu_itlb_read data array %08x", addr);
  }

  /* return an invalid entry */
  return 0;
}

uint32_t sh4_mmu_utlb_read(struct sh4 *sh4, uint32_t addr, uint32_t mask) {
  if (addr < 0x01000000) {
    LOG_MMU("sh4_mmu_utlb_read address array %08x", addr);

    struct sh4_tlb_entry *entry = &sh4->utlb[TLB_INDEX(addr)];
    uint32_t data = entry->hi.full;
    data |= entry->lo.D << 9;
    data |= entry->lo.V << 8;
    return data;
  } else {
    if (addr & 0x800000) {
      LOG_FATAL("sh4_mmu_utlb_read data array 2 %08x", addr);
    } else {
      LOG_MMU("sh4_mmu_utlb_read data array 1 %08x", addr);

      struct sh4_tlb_entry *entry = &sh4->utlb[TLB_INDEX(addr)];
      uint32_t data = entry->lo.full;
      return data;
    }
  }
}

void sh4_mmu_itlb_write(struct sh4 *sh4, uint32_t addr, uint32_t data,
                        uint32_t mask) {
  if (addr < 0x01000000) {
    LOG_MMU("sh4_mmu_itlb_write address array %08x %08x", addr, data);
  } else {
    LOG_MMU("sh4_mmu_itlb_write data array %08x %08x", addr, data);
  }

  /* ignore */
}

void sh4_mmu_utlb_write(struct sh4 *sh4, uint32_t addr, uint32_t data,
                        uint32_t mask) {
  if (addr < 0x01000000) {
    if (addr & 0x80) {
      LOG_FATAL("sh4_mmu_utlb_write address array (associative) %08x %08x",
                addr, data);
    } else {
      LOG_MMU("sh4_mmu_utlb_write address array %08x %08x", addr, data);

      struct sh4_tlb_entry *entry = &sh4->utlb[TLB_INDEX(addr)];
      entry->hi.full = data & 0xfffffcff;
      entry->lo.D = (data >> 9) & 1;
      entry->lo.V = (data >> 8) & 1;

      sh4_mmu_utlb_sync(sh4, entry);
    }
  } else {
    if (addr & 0x800000) {
      LOG_FATAL("sh4_mmu_utlb_write data array 2 %08x %08x", addr, data);
    } else {
      LOG_MMU("sh4_mmu_utlb_write data array 1 %08x %08x", addr, data);

      struct sh4_tlb_entry *entry = &sh4->utlb[TLB_INDEX(addr)];
      entry->lo.full = data;

      sh4_mmu_utlb_sync(sh4, entry);
    }
  }
}
