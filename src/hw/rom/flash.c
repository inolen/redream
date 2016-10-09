#include <stdio.h>
#include "hw/rom/flash.h"
#include "core/option.h"
#include "hw/dreamcast.h"

DEFINE_OPTION_STRING(flash, "dc_flash.bin", "Path to flash ROM");

// there doesn't seem to exist any documentation on the actual flashrom used
// by the dreamcast, however, there are several people who have replaced it
// with the MX29LV160 successfully
#define FLASH_SIZE 0x00020000

struct flash {
  struct device;
  uint8_t rom[FLASH_SIZE];
};

static uint32_t flash_rom_read(struct flash *flash, uint32_t addr,
                               uint32_t data_mask) {
  return READ_DATA(&flash->rom[addr]);
}

static void flash_rom_write(struct flash *flash, uint32_t addr, uint32_t data,
                            uint32_t data_mask) {
  WRITE_DATA(&flash->rom[addr]);
}

static int flash_load(struct flash *flash, const char *path) {
  FILE *fp = fopen(path, "rb");
  if (!fp) {
    LOG_WARNING("Failed to open flash at \"%s\"", path);
    return 0;
  }

  fseek(fp, 0, SEEK_END);
  int size = ftell(fp);
  fseek(fp, 0, SEEK_SET);

  if (size != FLASH_SIZE) {
    LOG_WARNING("Flash size mismatch, is %d, expected %d", size, FLASH_SIZE);
    fclose(fp);
    return 0;
  }

  int n = (int)fread(flash->rom, sizeof(uint8_t), size, fp);
  fclose(fp);

  if (n != size) {
    LOG_WARNING("Flash read failed");
    return 0;
  }

  return 1;
}

static bool flash_init(struct device *dev) {
  struct flash *flash = (struct flash *)dev;

  if (!flash_load(flash, OPTION_flash)) {
    return false;
  }

  return true;
}

struct flash *flash_create(struct dreamcast *dc) {
  struct flash *flash =
      dc_create_device(dc, sizeof(struct flash), "flash", &flash_init);
  return flash;
}

void flash_destroy(struct flash *flash) {
  dc_destroy_device((struct device *)flash);
}

// clang-format off
AM_BEGIN(struct flash, flash_rom_map);
  AM_RANGE(0x00000000, 0x0001ffff) AM_HANDLE("flash rom",
                                             (mmio_read_cb)&flash_rom_read,
                                             (mmio_write_cb)&flash_rom_write)
AM_END();
// clang-format on
