#include <stdio.h>
#include "hw/rom/flash.h"
#include "core/filesystem.h"
#include "core/option.h"
#include "dreamcast.h"

#define FLASH_SIZE 0x00020000
#define FLASH_SECTOR_SIZE 0x4000

/* there doesn't seem to be any documentation on the flash rom used by the
   dreamcast, but it appears to implement the JEDEC CFI standard */
#define FLASH_CMD_NONE 0x0
#define FLASH_CMD_ERASE 0x80
#define FLASH_CMD_ERASE_CHIP 0x10
#define FLASH_CMD_ERASE_SECTOR 0x30
#define FLASH_CMD_PROGRAM 0xa0

struct flash {
  struct device;

  uint8_t rom[FLASH_SIZE];

  /* cmd parsing state */
  int cmd;
  int cmd_state;
};

static const char *flash_bin_path() {
  static char filename[PATH_MAX];

  if (!filename[0]) {
    const char *appdir = fs_appdir();
    snprintf(filename, sizeof(filename), "%s" PATH_SEPARATOR "flash.bin",
             appdir);
  }

  return filename;
}

static uint32_t flash_cmd_read(struct flash *flash, uint32_t addr,
                               uint32_t data_mask) {
  int size = DATA_SIZE();
  uint32_t mem;
  flash_read(flash, addr, &mem, size);
  return mem & data_mask;
}

static void flash_save_rom(struct flash *flash) {
  const char *filename = flash_bin_path();

  FILE *fp = fopen(filename, "wb");
  int n = (int)fwrite(flash->rom, 1, sizeof(flash->rom), fp);
  CHECK_EQ(n, (int)sizeof(flash->rom));
  fclose(fp);
}

static int flash_load_rom(struct flash *flash) {
  const char *filename = flash_bin_path();

  FILE *fp = fopen(filename, "rb");
  if (!fp) {
    LOG_WARNING("failed to load %s", filename);
    return 0;
  }

  fseek(fp, 0, SEEK_END);
  int size = ftell(fp);
  fseek(fp, 0, SEEK_SET);

  if (size != sizeof(flash->rom)) {
    LOG_WARNING("flash size mismatch, is %d, expected %d", size,
                (int)sizeof(flash->rom));
    fclose(fp);
    return 0;
  }

  int n = (int)fread(flash->rom, 1, size, fp);
  CHECK_EQ(n, size);
  fclose(fp);

  return 1;
}

static void flash_cmd_program(struct flash *flash, uint32_t addr, uint32_t data,
                              uint32_t data_mask) {
  /* programming can only clear bits to 0 */
  int size = DATA_SIZE();
  uint32_t mem;
  flash_read(flash, addr, &mem, size);
  mem &= data;
  flash_write(flash, addr, &mem, size);
}

static void flash_cmd_erase_chip(struct flash *flash) {
  /* erasing resets bits to 1 */
  uint8_t empty_chip[FLASH_SIZE];
  memset(empty_chip, 0xff, sizeof(empty_chip));
  flash_write(flash, 0, empty_chip, sizeof(empty_chip));
}

static void flash_cmd_erase_sector(struct flash *flash, uint32_t addr) {
  /* round address down to the nearest sector start */
  addr &= ~(FLASH_SECTOR_SIZE - 1);

  /* erasing resets bits to 1 */
  uint8_t empty_sector[FLASH_SECTOR_SIZE];
  memset(empty_sector, 0xff, sizeof(empty_sector));
  flash_write(flash, addr, empty_sector, sizeof(empty_sector));
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
      LOG_FATAL("unexpected flash command state %d", flash->cmd_state);
      break;
  }
}

static int flash_init(struct device *dev) {
  struct flash *flash = (struct flash *)dev;

  /* attempt to load the flash rom, if this fails the bios should reset the
     flash to a valid state */
  flash_load_rom(flash);

  return 1;
}

void flash_write(struct flash *flash, int offset, const void *data, int size) {
  CHECK(offset >= 0 && (offset + size) <= (int)sizeof(flash->rom));
  memcpy(&flash->rom[offset], data, size);
}

void flash_read(struct flash *flash, int offset, void *data, int size) {
  CHECK(offset >= 0 && (offset + size) <= (int)sizeof(flash->rom));
  memcpy(data, &flash->rom[offset], size);
}

void flash_destroy(struct flash *flash) {
  flash_save_rom(flash);
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
