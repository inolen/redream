#include <stdio.h>
#include "hw/rom/bios.h"
#include "core/md5.h"
#include "core/option.h"
#include "hw/dreamcast.h"

DEFINE_OPTION_STRING(bios, "dc_boot.bin", "Path to boot rom");

#define BIOS_SIZE 0x00200000

struct bios {
  struct device;
  uint8_t rom[BIOS_SIZE];
};

static uint32_t bios_rom_read(struct bios *bios, uint32_t addr,
                              uint32_t data_mask) {
  return READ_DATA(&bios->rom[addr]);
}

static int bios_validate(struct bios *bios) {
  static const char *valid_bios_md5[] = {
      "a5c6a00818f97c5e3e91569ee22416dc", /* chinese bios */
      "37c921eb47532cae8fb70e5d987ce91c", /* japanese bios */
      "f2cd29d09f3e29984bcea22ab2e006fe", /* revised bios w/o MIL-CD */
      "e10c53c2f8b90bab96ead2d368858623"  /* original US/EU bios */
  };

  /* compare the rom's md5 against known good bios roms */
  MD5_CTX md5_ctx;
  MD5_Init(&md5_ctx);
  MD5_Update(&md5_ctx, bios->rom, BIOS_SIZE);
  char result[33];
  MD5_Final(result, &md5_ctx);

  for (int i = 0; i < array_size(valid_bios_md5); ++i) {
    if (strcmp(result, valid_bios_md5[i]) == 0) {
      return 1;
    }
  }

  return 0;
}

static int bios_load_rom(struct bios *bios, const char *path) {
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

  int n = (int)fread(bios->rom, sizeof(uint8_t), size, fp);
  CHECK_EQ(n, size);
  fclose(fp);

  if (!bios_validate(bios)) {
    LOG_WARNING("Invalid BIOS file");
    return 0;
  }

  return 1;
}

void bios_destroy(struct bios *bios) {
  dc_destroy_device((struct device *)bios);
}

static int bios_init(struct device *dev) {
  struct bios *bios = (struct bios *)dev;

  if (!bios_load_rom(bios, OPTION_bios)) {
    LOG_WARNING("Failed to load boot rom");
    return 0;
  }

  return 1;
}

struct bios *bios_create(struct dreamcast *dc) {
  struct bios *bios =
      dc_create_device(dc, sizeof(struct bios), "bios", &bios_init, NULL);
  return bios;
  ;
}

/* clang-format off */
AM_BEGIN(struct bios, boot_rom_map);
  AM_RANGE(0x00000000, 0x001fffff) AM_HANDLE("boot rom",
                                             (mmio_read_cb)&bios_rom_read,
                                             NULL,
                                             NULL, NULL)
AM_END();
/* clang-format on */
