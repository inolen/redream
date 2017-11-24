#include "guest/aica/aica.h"
#include "guest/holly/holly.h"
#include "guest/memory.h"
#include "guest/pvr/pvr.h"
#include "guest/pvr/ta.h"
#include "guest/rom/boot.h"
#include "guest/rom/flash.h"
#include "guest/sh4/sh4.h"

static uint32_t sh4_reg_read(struct sh4 *sh4, uint32_t addr, uint32_t mask) {
  uint32_t offset = SH4_REG_OFFSET(addr);
  reg_read_cb read = sh4_cb[offset].read;

  uint32_t data;
  if (read) {
    data = read(sh4->dc);
  } else {
    data = sh4->reg[offset];
  }

  if (sh4->log_regs) {
    LOG_INFO("sh4_reg_read addr=0x%08x data=0x%x", addr, data);
  }

  return data;
}

static void sh4_reg_write(struct sh4 *sh4, uint32_t addr, uint32_t data,
                          uint32_t mask) {
  uint32_t offset = SH4_REG_OFFSET(addr);
  reg_write_cb write = sh4_cb[offset].write;

  if (sh4->log_regs) {
    LOG_INFO("sh4_reg_write addr=0x%08x data=0x%x", addr, data & mask);
  }

  if (write) {
    write(sh4->dc, data);
    return;
  }

  sh4->reg[offset] = data;
}

void sh4_p4_write(struct sh4 *sh4, uint32_t addr, uint32_t data,
                  uint32_t mask) {
  if (addr >= SH4_SQ_BEGIN && addr <= SH4_SQ_END) {
    sh4_ccn_sq_write(sh4, addr - SH4_SQ_BEGIN, data, mask);
  } else if (addr >= SH4_ICACHE_BEGIN && addr <= SH4_ICACHE_END) {
    sh4_ccn_icache_write(sh4, addr - SH4_ICACHE_BEGIN, data, mask);
  } else if (addr >= SH4_ITLB_BEGIN && addr <= SH4_ITLB_END) {
    sh4_mmu_itlb_write(sh4, addr - SH4_ITLB_BEGIN, data, mask);
  } else if (addr >= SH4_OCACHE_BEGIN && addr <= SH4_OCACHE_END) {
    sh4_ccn_ocache_write(sh4, addr - SH4_OCACHE_BEGIN, data, mask);
  } else if (addr >= SH4_UTLB_BEGIN && addr <= SH4_UTLB_END) {
    sh4_mmu_utlb_write(sh4, addr - SH4_UTLB_BEGIN, data, mask);
  } else {
    LOG_FATAL("sh4_p4_write unexpected addr 0x%08x", addr);
  }
}

uint32_t sh4_p4_read(struct sh4 *sh4, uint32_t addr, uint32_t mask) {
  if (addr >= SH4_SQ_BEGIN && addr <= SH4_SQ_END) {
    return sh4_ccn_sq_read(sh4, addr - SH4_SQ_BEGIN, mask);
  } else if (addr >= SH4_ICACHE_BEGIN && addr <= SH4_ICACHE_END) {
    return sh4_ccn_icache_read(sh4, addr - SH4_ICACHE_BEGIN, mask);
  } else if (addr >= SH4_ITLB_BEGIN && addr <= SH4_ITLB_END) {
    return sh4_mmu_itlb_read(sh4, addr - SH4_ITLB_BEGIN, mask);
  } else if (addr >= SH4_OCACHE_BEGIN && addr <= SH4_OCACHE_END) {
    return sh4_ccn_ocache_read(sh4, addr - SH4_OCACHE_BEGIN, mask);
  } else if (addr >= SH4_UTLB_BEGIN && addr <= SH4_UTLB_END) {
    return sh4_mmu_utlb_read(sh4, addr - SH4_UTLB_BEGIN, mask);
  } else {
    LOG_FATAL("sh4_p4_read unexpected addr 0x%08x", addr);
  }
}

void sh4_area7_write(struct sh4 *sh4, uint32_t addr, uint32_t data,
                     uint32_t mask) {
  /* sh4 on-chip ram is only accessible from P0 */
  if (addr >= SH4_CACHE_BEGIN && addr <= SH4_CACHE_END) {
    sh4_ccn_cache_write(sh4, addr - SH4_CACHE_BEGIN, data, mask);
    return;
  }

  /* mask off upper bits creating p0-p4 mirrors */
  addr &= SH4_ADDR_MASK;

  if (addr >= SH4_REG_BEGIN && addr <= SH4_REG_END) {
    sh4_reg_write(sh4, addr - SH4_REG_BEGIN, data, mask);
  } else {
    LOG_FATAL("sh4_area7_write unexpected addr 0x%08x", addr);
  }
}

uint32_t sh4_area7_read(struct sh4 *sh4, uint32_t addr, uint32_t mask) {
  /* on-chip ram is only accessible from P0 */
  if (addr >= SH4_CACHE_BEGIN && addr <= SH4_CACHE_END) {
    return sh4_ccn_cache_read(sh4, addr - SH4_CACHE_BEGIN, mask);
  }

  /* mask off upper bits creating p0-p4 mirrors */
  addr &= SH4_ADDR_MASK;

  if (addr >= SH4_REG_BEGIN && addr <= SH4_REG_END) {
    return sh4_reg_read(sh4, addr - SH4_REG_BEGIN, mask);
  } else {
    LOG_FATAL("sh4_area7_read unexpected addr 0x%08x", addr);
  }
}

void sh4_area4_write(struct sh4 *sh4, uint32_t addr, const uint8_t *ptr,
                     int size) {
  struct dreamcast *dc = sh4->dc;

  addr &= SH4_ADDR_MASK;

  /* create the mirror */
  addr &= SH4_AREA4_ADDR_MASK;

  if (addr >= SH4_TA_POLY_BEGIN && addr <= SH4_TA_POLY_END) {
    ta_poly_write(dc->ta, addr, ptr, size);
  } else if (addr >= SH4_TA_YUV_BEGIN && addr <= SH4_TA_YUV_END) {
    ta_yuv_write(dc->ta, addr, ptr, size);
  } else if (addr >= SH4_TA_TEXTURE_BEGIN && addr <= SH4_TA_TEXTURE_END) {
    ta_texture_write(dc->ta, addr, ptr, size);
  } else {
    /* nop */
  }
}

uint32_t sh4_area4_read(struct sh4 *sh4, uint32_t addr, uint32_t mask) {
  addr &= SH4_ADDR_MASK;

  /* create the mirror */
  addr &= SH4_AREA4_ADDR_MASK;

  /* area 4 is read-only, but will return the physical address when accessed */
  return addr;
}

void sh4_area1_write(struct sh4 *sh4, uint32_t addr, uint32_t data,
                     uint32_t mask) {
  struct dreamcast *dc = sh4->dc;

  addr &= SH4_ADDR_MASK;

  /* create the mirror */
  addr &= SH4_AREA1_ADDR_MASK;

  if (addr >= SH4_PVR_VRAM64_BEGIN && addr <= SH4_PVR_VRAM64_END) {
    pvr_vram64_write(dc->pvr, addr - SH4_PVR_VRAM64_BEGIN, data, mask);
  } else if (addr >= SH4_PVR_VRAM32_BEGIN && addr <= SH4_PVR_VRAM32_END) {
    pvr_vram32_write(dc->pvr, addr - SH4_PVR_VRAM32_BEGIN, data, mask);
  } else {
    LOG_FATAL("sh4_area1_write unexpected addr 0x%08x", addr);
  }
}

uint32_t sh4_area1_read(struct sh4 *sh4, uint32_t addr, uint32_t mask) {
  struct dreamcast *dc = sh4->dc;

  addr &= SH4_ADDR_MASK;

  /* create the mirror */
  addr &= SH4_AREA1_ADDR_MASK;

  if (addr >= SH4_PVR_VRAM64_BEGIN && addr <= SH4_PVR_VRAM64_END) {
    return pvr_vram64_read(dc->pvr, addr - SH4_PVR_VRAM64_BEGIN, mask);
  } else if (addr >= SH4_PVR_VRAM32_BEGIN && addr <= SH4_PVR_VRAM32_END) {
    return pvr_vram32_read(dc->pvr, addr - SH4_PVR_VRAM32_BEGIN, mask);
  } else {
    LOG_FATAL("sh4_area1_read unexpected addr 0x%08x", addr);
  }
}

void sh4_area0_write(struct sh4 *sh4, uint32_t addr, uint32_t data,
                     uint32_t mask) {
  struct dreamcast *dc = sh4->dc;

  /* mask off upper bits creating p0-p4 mirrors */
  addr &= SH4_ADDR_MASK;

  /* flash rom is not accessible in the area 0 mirror */
  if (addr >= SH4_FLASH_ROM_BEGIN && addr <= SH4_FLASH_ROM_END) {
    flash_rom_write(dc->flash, addr - SH4_FLASH_ROM_BEGIN, data, mask);
    return;
  }

  /* create the mirror */
  addr &= SH4_AREA0_ADDR_MASK;

  if (/*addr >= SH4_BOOT_ROM_BEGIN*/ addr <= SH4_BOOT_ROM_END) {
    /* read-only */
  } else if (addr >= SH4_HOLLY_REG_BEGIN && addr <= SH4_HOLLY_REG_END) {
    holly_reg_write(dc->holly, addr - SH4_HOLLY_REG_BEGIN, data, mask);
  } else if (addr >= SH4_PVR_REG_BEGIN && addr <= SH4_PVR_REG_END) {
    pvr_reg_write(dc->pvr, addr - SH4_PVR_REG_BEGIN, data, mask);
  } else if (addr >= SH4_MODEM_BEGIN && addr <= SH4_MODEM_END) {
    /* nop */
  } else if (addr >= SH4_AICA_REG_BEGIN && addr <= SH4_AICA_REG_END) {
    aica_reg_write(dc->aica, addr - SH4_AICA_REG_BEGIN, data, mask);
  } else if (addr >= SH4_AICA_MEM_BEGIN && addr <= SH4_AICA_MEM_END) {
    aica_mem_write(dc->aica, addr - SH4_AICA_MEM_BEGIN, data, mask);
  } else if (addr >= SH4_HOLLY_EXT_BEGIN && addr <= SH4_HOLLY_EXT_END) {
    /* nop */
  } else {
    LOG_FATAL("sh4_area0_write unexpected addr 0x%08x", addr);
  }
}

uint32_t sh4_area0_read(struct sh4 *sh4, uint32_t addr, uint32_t mask) {
  struct dreamcast *dc = sh4->dc;

  /* mask off upper bits creating p0-p4 mirrors */
  addr &= SH4_ADDR_MASK;

  /* boot / flash rom are not accessible in the area 0 mirror */
  if (/*addr >= SH4_BOOT_ROM_BEGIN &&*/ addr <= SH4_BOOT_ROM_END) {
    return boot_rom_read(dc->boot, addr - SH4_BOOT_ROM_BEGIN, mask);
  } else if (addr >= SH4_FLASH_ROM_BEGIN && addr <= SH4_FLASH_ROM_END) {
    return flash_rom_read(dc->flash, addr - SH4_FLASH_ROM_BEGIN, mask);
  }

  /* create the mirror */
  addr &= SH4_AREA0_ADDR_MASK;

  if (/*addr >= SH4_BOOT_ROM_BEGIN*/ addr <= SH4_BOOT_ROM_END) {
    return 0xffffffff;
  } else if (addr >= SH4_FLASH_ROM_BEGIN && addr <= SH4_FLASH_ROM_END) {
    return 0xffffffff;
  } else if (addr >= SH4_HOLLY_REG_BEGIN && addr <= SH4_HOLLY_REG_END) {
    return holly_reg_read(dc->holly, addr - SH4_HOLLY_REG_BEGIN, mask);
  } else if (addr >= SH4_PVR_REG_BEGIN && addr <= SH4_PVR_REG_END) {
    return pvr_reg_read(dc->pvr, addr - SH4_PVR_REG_BEGIN, mask);
  } else if (addr >= SH4_MODEM_BEGIN && addr <= SH4_MODEM_END) {
    return 0;
  } else if (addr >= SH4_AICA_REG_BEGIN && addr <= SH4_AICA_REG_END) {
    return aica_reg_read(dc->aica, addr - SH4_AICA_REG_BEGIN, mask);
  } else if (addr >= SH4_AICA_MEM_BEGIN && addr <= SH4_AICA_MEM_END) {
    return aica_mem_read(dc->aica, addr - SH4_AICA_MEM_BEGIN, mask);
  } else if (addr >= SH4_HOLLY_EXT_BEGIN && addr <= SH4_HOLLY_EXT_END) {
    return 0;
  } else {
    LOG_FATAL("sh4_area0_read unexpected addr 0x%08x", addr);
  }
}
