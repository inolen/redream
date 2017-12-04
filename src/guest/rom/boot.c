#include "guest/rom/boot.h"
#include "core/filesystem.h"
#include "core/md5.h"
#include "guest/dreamcast.h"
#include "guest/memory.h"

struct boot {
  struct device;
  uint8_t rom[0x00200000];
};

static const char *boot_bin_path() {
  static char filename[PATH_MAX];

  if (!filename[0]) {
    const char *appdir = fs_appdir();
    snprintf(filename, sizeof(filename), "%s" PATH_SEPARATOR "boot.bin",
             appdir);
  }

  return filename;
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

  for (int i = 0; i < ARRAY_SIZE(valid_bios_md5); ++i) {
    if (strcmp(result, valid_bios_md5[i]) == 0) {
      return 1;
    }
  }

  return 0;
}

static int boot_load_rom(struct boot *boot) {
  const char *filename = boot_bin_path();

  LOG_INFO("boot_load_rom path=%s", filename);

  FILE *fp = fopen(filename, "rb");
  if (!fp) {
    LOG_WARNING("boot_load_rom failed to open");
    return 0;
  }

  fseek(fp, 0, SEEK_END);
  int size = ftell(fp);
  fseek(fp, 0, SEEK_SET);

  if (size != (int)sizeof(boot->rom)) {
    LOG_WARNING("boot_load_rom size mismatch size=%d expected=%d", size,
                sizeof(boot->rom));
    fclose(fp);
    return 0;
  }

  int n = (int)fread(boot->rom, sizeof(uint8_t), size, fp);
  CHECK_EQ(n, size);
  fclose(fp);

  if (!boot_validate(boot)) {
    LOG_WARNING("boot_load_rom failed to validate");
    return 0;
  }

  return 1;
}

static int boot_init(struct device *dev) {
  struct boot *boot = (struct boot *)dev;

  /* attempt to load the boot rom, if this fails, the bios code will hle it */
  boot_load_rom(boot);

  return 1;
}

void boot_rom_write(struct boot *boot, uint32_t addr, uint32_t data,
                    uint32_t mask) {
  WRITE_DATA(&boot->rom[addr]);
}

uint32_t boot_rom_read(struct boot *boot, uint32_t addr, uint32_t mask) {
  return READ_DATA(&boot->rom[addr]);
}

void boot_destroy(struct boot *boot) {
  dc_destroy_device((struct device *)boot);
}

struct boot *boot_create(struct dreamcast *dc) {
  struct boot *boot =
      dc_create_device(dc, sizeof(struct boot), "boot", &boot_init, NULL);
  return boot;
}
