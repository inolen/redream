#include <stdio.h>
#include "hw/rom/flash.h"
#include "core/option.h"
#include "hw/dreamcast.h"
#include "sys/filesystem.h"

DEFINE_OPTION_STRING(flash, "dc_flash.bin", "Path to flash ROM");

// there doesn't seem to be any documentation on the flash rom used by thae
// dreamcast. however, several people have replaced it with the MX29LV160TMC-90
// successfully. the implementation of the command parsing here is based on its
// datasheet. note, the dreamcast seems to only use the word mode command
// sequences, so that is all that is implemented

#define FLASH_SIZE 0x00020000
#define SECTOR_SIZE 0x4000
#define CMD_NONE 0x0
#define CMD_ERASE 0x80
#define CMD_ERASE_CHIP 0x10
#define CMD_ERASE_SECTOR 0x30
#define CMD_PROGRAM 0xa0

struct flash {
  struct device;
  // path to persistent flash rom kept in the application directory
  char app_path[PATH_MAX];
  // cmd parsing state
  int cmd;
  int cmd_state;
  // rom data
  uint8_t rom[FLASH_SIZE];
};

static int flash_load_rom(struct flash *flash, const char *path) {
  FILE *fp = fopen(path, "rb");
  if (!fp) {
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
  CHECK_EQ(n, size);
  fclose(fp);

  return 1;
}

static int flash_save_rom(struct flash *flash, const char *path) {
  FILE *fp = fopen(path, "wb");
  if (!fp) {
    return 0;
  }

  int n = (int)fwrite(flash->rom, 1, FLASH_SIZE, fp);
  CHECK_EQ(n, FLASH_SIZE);
  fclose(fp);

  return 1;
}

static uint32_t flash_cmd_read(struct flash *flash, uint32_t addr, uint32_t data_mask) {
  uint32_t *mem = (uint32_t *)&flash->rom[addr];
  return *mem & data_mask;
}

static void flash_cmd_program(struct flash *flash, uint32_t addr, uint32_t data,
                              uint32_t data_mask) {
  uint32_t *mem = (uint32_t *)&flash->rom[addr];
  *mem &= ~data_mask | data;

  // update persistent copy of the flash rom
  CHECK(flash_save_rom(flash, flash->app_path));
}

static void flash_cmd_erase_chip(struct flash *flash) {
  memset(flash->rom, 0xff, FLASH_SIZE);
}

static void flash_cmd_erase_sector(struct flash *flash, uint32_t addr) {
  addr &= ~(SECTOR_SIZE - 1);
  memset(&flash->rom[addr], 0xff, SECTOR_SIZE);
}

static uint32_t flash_read(struct flash *flash, uint32_t addr,
                           uint32_t data_mask) {
  CHECK_EQ(flash->cmd_state, 0);
  return flash_cmd_read(flash, addr, data_mask);
}

static void flash_write(struct flash *flash, uint32_t addr, uint32_t data,
                        uint32_t data_mask) {
  switch (flash->cmd_state) {
    case 0: {
      CHECK(addr == 0x5555 && data == 0xaa);
      flash->cmd_state++;
    } break;

    case 1: {
      CHECK(addr == 0x2aaa && data == 0x55);
      flash->cmd_state++;
    } break;

    case 2: {
      CHECK(addr == 0x5555 && (data == CMD_ERASE || data == CMD_PROGRAM));
      flash->cmd = data;
      flash->cmd_state++;
    } break;

    case 3: {
      if (flash->cmd == CMD_PROGRAM) {
        flash_cmd_program(flash, addr, data, data_mask);
        flash->cmd_state = 0;
      } else {
        CHECK_EQ(flash->cmd, CMD_ERASE);
        CHECK(addr == 0x5555 && data == 0xaa);
        flash->cmd_state++;
      }
    } break;

    case 4: {
      CHECK(addr == 0x2aaa && data == 0x55);
      flash->cmd_state++;
    } break;

    case 5: {
      if (data == CMD_ERASE_CHIP) {
        CHECK(addr == 0x5555);
        flash_cmd_erase_chip(flash);
      } else {
        CHECK_EQ(data, CMD_ERASE_SECTOR);
        flash_cmd_erase_sector(flash, addr);
      }
      flash->cmd_state = 0;
    } break;

    default:
      LOG_FATAL("Unexpected flash command state %d", flash->cmd_state);
      break;
  }
}

static bool flash_init(struct device *dev) {
  struct flash *flash = (struct flash *)dev;

  // keep a persistent copy of the flash rom in the application directory
  const char *appdir = fs_appdir();
  snprintf(flash->app_path, sizeof(flash->app_path),
           "%s" PATH_SEPARATOR "flash.bin", appdir);

  // attempt to load flash rom from the application directory first, falling
  // back to the command line path if it doesn't exist
  if (!(flash_load_rom(flash, flash->app_path) ||
        flash_load_rom(flash, OPTION_flash))) {
    LOG_WARNING("Failed to load flash rom");
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
                                             (mmio_read_cb)&flash_read,
                                             (mmio_write_cb)&flash_write)
AM_END();
// clang-format on
