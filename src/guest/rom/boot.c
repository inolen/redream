#include "guest/rom/boot.h"
#include "core/filesystem.h"
#include "core/md5.h"
#include "core/option.h"
#include "guest/dreamcast.h"

DEFINE_OPTION_STRING(bios, "dc_boot.bin", "Path to boot rom");

struct boot {
  struct device;
  uint8_t rom[0x00200000];
};

static uint32_t boot_rom_read(struct boot *boot, uint32_t addr,
                              uint32_t data_mask) {
  return READ_DATA(&boot->rom[addr]);
}

static int boot_validate(struct boot *boot) {
  static const char *valid_bios_md5[] = {
      "a5c6a00818f97c5e3e91569ee22416dc", /* chinese bios */
      "37c921eb47532cae8fb70e5d987ce91c", /* japanese bios */
      "f2cd29d09f3e29984bcea22ab2e006fe", /* revised bios w/o MIL-CD */
      "e10c53c2f8b90bab96ead2d368858623"  /* original US/EU bios */
  };

  /* compare the rom's md5 against known good bios roms */
  MD5_CTX md5_ctx;
  MD5_Init(&md5_ctx);
  MD5_Update(&md5_ctx, boot->rom, sizeof(boot->rom));
  char result[33];
  MD5_Final(result, &md5_ctx);

  for (int i = 0; i < array_size(valid_bios_md5); ++i) {
    if (strcmp(result, valid_bios_md5[i]) == 0) {
      return 1;
    }
  }

  return 0;
}

static int boot_load_rom(struct boot *boot) {
  const char *filename = OPTION_bios;

  FILE *fp = fopen(filename, "rb");
  if (!fp) {
    return 0;
  }

  fseek(fp, 0, SEEK_END);
  int size = ftell(fp);
  fseek(fp, 0, SEEK_SET);

  if (size != (int)sizeof(boot->rom)) {
    LOG_WARNING("boot rom size mismatch, is %d, expected %d", size,
                sizeof(boot->rom));
    fclose(fp);
    return 0;
  }

  int n = (int)fread(boot->rom, sizeof(uint8_t), size, fp);
  CHECK_EQ(n, size);
  fclose(fp);

  if (!boot_validate(boot)) {
    LOG_WARNING("failed to validate boot rom");
    return 0;
  }

  LOG_INFO("boot_load_rom loaded '%s'", filename);

  return 1;
}

static int boot_init(struct device *dev) {
  struct boot *boot = (struct boot *)dev;

  /* attempt to load the boot rom, if this fails, the bios code will hle it */
  boot_load_rom(boot);

  return 1;
}

void boot_write(struct boot *boot, int offset, const void *data, int n) {
  CHECK(offset >= 0 && (offset + n) <= (int)sizeof(boot->rom));

  memcpy(&boot->rom[offset], data, n);
}

void boot_read(struct boot *boot, int offset, void *data, int n) {
  CHECK(offset >= 0 && (offset + n) <= (int)sizeof(boot->rom));

  memcpy(data, &boot->rom[offset], n);
}

void boot_destroy(struct boot *boot) {
  dc_destroy_device((struct device *)boot);
}

struct boot *boot_create(struct dreamcast *dc) {
  struct boot *boot =
      dc_create_device(dc, sizeof(struct boot), "boot", &boot_init);
  return boot;
}

/* clang-format off */
AM_BEGIN(struct boot, boot_rom_map);
  AM_RANGE(0x00000000, 0x001fffff) AM_HANDLE("boot rom",
                                             (mmio_read_cb)&boot_rom_read,
                                             NULL,
                                             NULL, NULL)
AM_END();
/* clang-format on */
