#include "guest/rom/flash.h"
#include "core/filesystem.h"
#include "guest/dreamcast.h"
#include "guest/memory.h"

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

  uint8_t rom[0x00020000];

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
    LOG_WARNING("failed to open flash rom '%s'", filename);
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

static uint32_t flash_cmd_read(struct flash *flash, uint32_t addr,
                               uint32_t mask) {
  int size = DATA_SIZE();
  uint32_t mem;
  flash_read(flash, addr, &mem, size);
  return mem;
}

static void flash_cmd_program(struct flash *flash, uint32_t addr, uint32_t data,
                              uint32_t mask) {
  int size = DATA_SIZE();
  flash_program(flash, addr, &data, size);
}

static void flash_cmd_erase_chip(struct flash *flash) {
  int size = sizeof(flash->rom);
  flash_erase(flash, 0, size);
}

static void flash_cmd_erase_sector(struct flash *flash, uint32_t addr) {
  /* round address down to the nearest sector start */
  addr &= ~(FLASH_SECTOR_SIZE - 1);

  flash_erase(flash, addr, FLASH_SECTOR_SIZE);
}

static int flash_init(struct device *dev) {
  struct flash *flash = (struct flash *)dev;

  /* attempt to load the flash rom, if this fails the bios will reset it */
  flash_load_rom(flash);

  return 1;
}

void flash_erase(struct flash *flash, int offset, int n) {
  CHECK(offset >= 0 && (offset + n) <= (int)sizeof(flash->rom));

  /* erasing resets bits to 1 */
  memset(&flash->rom[offset], 0xff, n);
}

void flash_program(struct flash *flash, int offset, const void *data, int n) {
  CHECK(offset >= 0 && (offset + n) <= (int)sizeof(flash->rom));

  const uint8_t *bytes = data;

  /* programming can only clear bits to 0 */
  for (int i = 0; i < n; i++) {
    flash->rom[offset + i] &= bytes[i];
  }
}

void flash_write(struct flash *flash, int offset, const void *data, int n) {
  CHECK(offset >= 0 && (offset + n) <= (int)sizeof(flash->rom));

  memcpy(&flash->rom[offset], data, n);
}

void flash_read(struct flash *flash, int offset, void *data, int n) {
  CHECK(offset >= 0 && (offset + n) <= (int)sizeof(flash->rom));

  memcpy(data, &flash->rom[offset], n);
}

void flash_rom_write(struct flash *flash, uint32_t addr, uint32_t data,
                     uint32_t mask) {
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
        flash_cmd_program(flash, addr, data, mask);
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

uint32_t flash_rom_read(struct flash *flash, uint32_t addr, uint32_t mask) {
  CHECK_EQ(flash->cmd_state, 0);
  return flash_cmd_read(flash, addr, mask);
}

void flash_destroy(struct flash *flash) {
  flash_save_rom(flash);
  dc_destroy_device((struct device *)flash);
}

struct flash *flash_create(struct dreamcast *dc) {
  struct flash *flash =
      dc_create_device(dc, sizeof(struct flash), "flash", &flash_init, NULL);

  return flash;
}
