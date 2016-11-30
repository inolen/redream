#include <stdio.h>
#include "hw/rom/boot.h"
#include "core/option.h"
#include "hw/dreamcast.h"

DEFINE_OPTION_STRING(bios, "dc_boot.bin", "Path to boot rom");

#define BIOS_SIZE 0x00200000

struct boot {
  struct device;
  uint8_t rom[BIOS_SIZE];
};

static uint32_t boot_rom_read(struct boot *boot, uint32_t addr,
                              uint32_t data_mask) {
  return READ_DATA(&boot->rom[addr]);
}

static void boot_rom_write(struct boot *boot, uint32_t addr, uint32_t data,
                           uint32_t data_mask) {
  LOG_FATAL("Can't write to boot rom");
}

static int boot_load_rom(struct boot *boot, const char *path) {
  FILE *fp = fopen(path, "rb");
  if (!fp) {
    return 0;
  }

  fseek(fp, 0, SEEK_END);
  int size = ftell(fp);
  fseek(fp, 0, SEEK_SET);

  if (size != BIOS_SIZE) {
    LOG_WARNING("Boot rom size mismatch, is %d, expected %d", size, BIOS_SIZE);
    fclose(fp);
    return 0;
  }

  int n = (int)fread(boot->rom, sizeof(uint8_t), size, fp);
  CHECK_EQ(n, size);
  fclose(fp);

  return 1;
}

static bool boot_init(struct device *dev) {
  struct boot *boot = (struct boot *)dev;

  if (!boot_load_rom(boot, OPTION_bios)) {
    LOG_WARNING("Failed to load boot rom");
    return false;
  }

  return true;
}

void boot_destroy(struct boot *boot) {
  dc_destroy_device((struct device *)boot);
}

struct boot *boot_create(struct dreamcast *dc) {
  struct boot *boot =
      dc_create_device(dc, sizeof(struct boot), "boot", &boot_init);
  return boot;
}

// clang-format off
AM_BEGIN(struct boot, boot_rom_map);
  AM_RANGE(0x00000000, 0x001fffff) AM_HANDLE("boot rom",
                                             (mmio_read_cb)&boot_rom_read,
                                             (mmio_write_cb)&boot_rom_write)
AM_END();
// clang-format on
