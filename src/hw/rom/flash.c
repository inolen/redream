/* there doesn't seem to be any documentation on the flash rom used by the
   dreamcast, but it appears to implement the JEDEC CFI standard */

#include <stdio.h>
#include "hw/rom/flash.h"
#include "core/option.h"
#include "hw/dreamcast.h"
#include "sys/filesystem.h"

DEFINE_OPTION_STRING(flash, "dc_flash.bin", "Path to flash rom");

#define FLASH_SIZE 0x00020000
#define FLASH_SECTOR_SIZE 0x4000
#define FLASH_CMD_NONE 0x0
#define FLASH_CMD_ERASE 0x80
#define FLASH_CMD_ERASE_CHIP 0x10
#define FLASH_CMD_ERASE_SECTOR 0x30
#define FLASH_CMD_PROGRAM 0xa0

struct flash {
  struct device;
  /* cmd parsing state */
  int cmd;
  int cmd_state;
};

const char *flash_bin_path() {
  static char filename[PATH_MAX];

  if (!filename[0]) {
    const char *appdir = fs_appdir();
    snprintf(filename, sizeof(filename), "%s" PATH_SEPARATOR "flash.bin",
             appdir);
  }

  return filename;
}

static void flash_read_bin(int offset, void *buffer, int size) {
  const char *flash_path = flash_bin_path();
  FILE *file = fopen(flash_path, "rb");
  CHECK_NOTNULL(file, "Failed to open %s", flash_path);
  int r = fseek(file, offset, SEEK_SET);
  CHECK_NE(r, -1);
  r = (int)fread(buffer, 1, size, file);
  CHECK_EQ(r, size);
  fclose(file);
}

static void flash_write_bin(int offset, const void *buffer, int size) {
  const char *flash_path = flash_bin_path();
  FILE *file = fopen(flash_path, "r+b");
  CHECK_NOTNULL(file, "Failed to open %s", flash_path);
  int r = fseek(file, offset, SEEK_SET);
  CHECK_NE(r, -1);
  r = (int)fwrite(buffer, 1, size, file);
  CHECK_EQ(r, size);
  fclose(file);
}

static int flash_init_bin() {
  /* check to see if a persistent flash rom exists in the app directory */
  const char *flash_path = flash_bin_path();
  if (fs_exists(flash_path)) {
    return 1;
  }

  /* if it doesn't, read the original flash rom from the command line path */
  uint8_t rom[FLASH_SIZE];

  LOG_INFO("Initializing flash rom from %s", OPTION_flash);

  FILE *src = fopen(OPTION_flash, "rb");
  if (!src) {
    LOG_WARNING("Failed to load %s", OPTION_flash);
    return 0;
  }

  fseek(src, 0, SEEK_END);
  int size = ftell(src);
  fseek(src, 0, SEEK_SET);

  if (size != FLASH_SIZE) {
    LOG_WARNING("Flash size mismatch, is %d, expected %d", size, FLASH_SIZE);
    fclose(src);
    return 0;
  }

  int n = (int)fread(rom, 1, size, src);
  CHECK_EQ(n, size);
  fclose(src);

  /* and copy it to the app directory */
  FILE *dst = fopen(flash_path, "wb");
  CHECK_NOTNULL("Failed to open %s", flash_path);
  int r = (int)fwrite(rom, 1, size, dst);
  CHECK_EQ(r, size);
  fclose(dst);

  return 1;
}

static uint32_t flash_cmd_read(struct flash *flash, uint32_t addr,
                               uint32_t data_mask) {
  int size = DATA_SIZE();
  uint32_t mem;
  flash_read_bin(addr, &mem, size);
  return mem & data_mask;
}

static void flash_cmd_program(struct flash *flash, uint32_t addr, uint32_t data,
                              uint32_t data_mask) {
  /* programming can only clear bits to 0 */
  int size = DATA_SIZE();
  uint32_t mem;
  flash_read_bin(addr, &mem, size);
  mem &= data;
  flash_write_bin(addr, &mem, size);
}

static void flash_cmd_erase_chip(struct flash *flash) {
  /* erasing resets bits to 1 */
  uint8_t empty_chip[FLASH_SIZE];
  memset(empty_chip, 0xff, sizeof(empty_chip));
  flash_write_bin(0, empty_chip, sizeof(empty_chip));
}

static void flash_cmd_erase_sector(struct flash *flash, uint32_t addr) {
  /* round address down to the nearest sector start */
  addr &= ~(FLASH_SECTOR_SIZE - 1);

  /* erasing resets bits to 1 */
  uint8_t empty_sector[FLASH_SECTOR_SIZE];
  memset(empty_sector, 0xff, sizeof(empty_sector));
  flash_write_bin(addr, empty_sector, sizeof(empty_sector));
}

static uint32_t flash_rom_read(struct flash *flash, uint32_t addr,
                               uint32_t data_mask) {
  CHECK_EQ(flash->cmd_state, 0);
  return flash_cmd_read(flash, addr, data_mask);
}

static void flash_rom_write(struct flash *flash, uint32_t addr, uint32_t data,
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
      CHECK(addr == 0x5555 &&
            (data == FLASH_CMD_ERASE || data == FLASH_CMD_PROGRAM));
      flash->cmd = data;
      flash->cmd_state++;
    } break;

    case 3: {
      if (flash->cmd == FLASH_CMD_PROGRAM) {
        flash_cmd_program(flash, addr, data, data_mask);
        flash->cmd_state = 0;
      } else {
        CHECK_EQ(flash->cmd, FLASH_CMD_ERASE);
        CHECK(addr == 0x5555 && data == 0xaa);
        flash->cmd_state++;
      }
    } break;

    case 4: {
      CHECK(addr == 0x2aaa && data == 0x55);
      flash->cmd_state++;
    } break;

    case 5: {
      if (data == FLASH_CMD_ERASE_CHIP) {
        CHECK(addr == 0x5555);
        flash_cmd_erase_chip(flash);
      } else {
        CHECK_EQ(data, FLASH_CMD_ERASE_SECTOR);
        flash_cmd_erase_sector(flash, addr);
      }
      flash->cmd_state = 0;
    } break;

    default:
      LOG_FATAL("Unexpected flash command state %d", flash->cmd_state);
      break;
  }
}

static int flash_init(struct device *dev) {
  struct flash *flash = (struct flash *)dev;

  if (!flash_init_bin()) {
    LOG_WARNING("Failed to load flash rom");
    return 0;
  }

  return 1;
}

void flash_destroy(struct flash *flash) {
  dc_destroy_device((struct device *)flash);
}

struct flash *flash_create(struct dreamcast *dc) {
  struct flash *flash =
      dc_create_device(dc, sizeof(struct flash), "flash", &flash_init);
  return flash;
}

/* clang-format off */
AM_BEGIN(struct flash, flash_rom_map);
  AM_RANGE(0x00000000, 0x0001ffff) AM_HANDLE("flash rom",
                                             (mmio_read_cb)&flash_rom_read,
                                             (mmio_write_cb)&flash_rom_write,
                                             NULL, NULL)
AM_END();
/* clang-format on */
