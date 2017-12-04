#include "guest/bios/bios.h"
#include "core/core.h"
#include "core/time.h"
#include "guest/aica/aica.h"
#include "guest/bios/bios.h"
#include "guest/bios/flash.h"
#include "guest/bios/scramble.h"
#include "guest/bios/syscalls.h"
#include "guest/dreamcast.h"
#include "guest/gdrom/gdrom.h"
#include "guest/memory.h"
#include "guest/rom/boot.h"
#include "guest/rom/flash.h"
#include "guest/sh4/sh4.h"
#include "options.h"

/* address of syscall vectors */
enum {
  VECTOR_SYSINFO = 0x0c0000b0,
  VECTOR_FONTROM = 0x0c0000b4,
  VECTOR_FLASHROM = 0x0c0000b8,
  VECTOR_GDROM = 0x0c0000bc,
  VECTOR_GDROM2 = 0x0c0000c0,
  VECTOR_SYSTEM = 0x0c0000e0,
};

/* address of syscall entrypoints */
enum {
  SYSCALL_SYSINFO = 0x0c003c00,
  SYSCALL_FONTROM = 0x0c003b80,
  SYSCALL_FLASHROM = 0x0c003d00,
  SYSCALL_GDROM = 0x0c001000,
  SYSCALL_GDROM2 = 0x0c0010f0,
  SYSCALL_SYSTEM = 0x0c000800,
};

static uint32_t bios_local_time() {
  /* dreamcast system time is relative to 1/1/1950 00:00 UTC, while the libc
     time functions are relative to 1/1/1970 00:00 UTC. subtract 20 years and
     5 leap days from the current time to match them up. note, mktime / difftime
     can't be used here with a tm struct filled out for 1950 as not all libc
     implementations support negative timestamps */
  time_t rawtime = time(NULL);
  struct tm localinfo = *localtime(&rawtime);
  struct tm gmtinfo = *gmtime(&rawtime);

  /* gmtime will set tm_isdst to 0. set to -1 to force mktime to check if the
     timestamp is in dst or not */
  gmtinfo.tm_isdst = -1;

  time_t localtime = mktime(&localinfo);
  time_t gmttime = mktime(&gmtinfo);
  double gmtdelta = difftime(gmttime, localtime);
  double gmtoffset = (20 * 365 + 5) * (24 * 60 * 60);
  return (uint32_t)(localtime - gmtdelta + gmtoffset);
}

static void bios_override_settings(struct bios *bios) {
  struct dreamcast *dc = bios->dc;
  struct flash *flash = dc->flash;

  int region = 0;
  int lang = 0;
  int bcast = 0;
  uint32_t time = bios_local_time();

  for (int i = 0; i < NUM_REGIONS; i++) {
    if (!strcmp(OPTION_region, REGIONS[i])) {
      region = i;
      break;
    }
  }

  for (int i = 0; i < NUM_LANGUAGES; i++) {
    if (!strcmp(OPTION_language, LANGUAGES[i])) {
      lang = i;
      break;
    }
  }

  for (int i = 0; i < NUM_BROADCASTS; i++) {
    if (!strcmp(OPTION_broadcast, BROADCASTS[i])) {
      bcast = i;
      break;
    }
  }

  LOG_INFO("bios_override_settings region=%s lang=%s bcast=%s time=0x%08x",
           REGIONS[region], LANGUAGES[lang], BROADCASTS[bcast], time);

  /* the region, language and broadcast settings exist in two locations:

     1. 0x8c000070-74. this data seems to be the "factory settings" and is read
        from 0x1a000 of the flash rom on init. this data is read-only

     2. 0x8c000078-7f. this data seems to be the "user settings" and is copied
        from partition 2, logical block 5 of the flash rom on init

     in order to force these settings, write to all of the locations in flash
     memory that they are ever read from */

  /* overwrite factory flash settings */
  char sysinfo[16];
  memcpy(sysinfo, "00000Dreamcast  ", sizeof(sysinfo));
  sysinfo[2] = '0' + region;
  sysinfo[3] = '0' + lang;
  sysinfo[4] = '0' + bcast;

  flash_write(flash, 0x1a000, sysinfo, sizeof(sysinfo));
  flash_write(flash, 0x1a0a0, sysinfo, sizeof(sysinfo));

  /* overwrite user flash settings */
  struct flash_syscfg_block syscfg;
  int res = flash_read_block(flash, FLASH_PT_USER, FLASH_USER_SYSCFG, &syscfg);

  if (!res) {
    /* write out default settings */
    memset(&syscfg, 0xff, sizeof(syscfg));
    syscfg.time_lo = 0;
    syscfg.time_hi = 0;
    syscfg.lang = 0;
    syscfg.mono = 0;
    syscfg.autostart = 1;
  }

  syscfg.time_lo = time & 0xffff;
  syscfg.time_hi = (time & 0xffff0000) >> 16;
  syscfg.lang = lang;

  res = flash_write_block(flash, FLASH_PT_USER, FLASH_USER_SYSCFG, &syscfg);
  CHECK_EQ(res, 1);

  /* overwrite aica clock to match the bios */
  aica_set_clock(dc->aica, time);
}

static void bios_validate_flash(struct bios *bios) {
  struct dreamcast *dc = bios->dc;
  struct flash *flash = dc->flash;

  /* validate partition 0 (factory settings) */
  {
    int valid = 1;
    char sysinfo[16];

    flash_read(flash, 0x1a000, sysinfo, sizeof(sysinfo));
    valid &= memcmp(&sysinfo[5], "Dreamcast  ", 11) == 0;

    flash_read(flash, 0x1a0a0, sysinfo, sizeof(sysinfo));
    valid &= memcmp(&sysinfo[5], "Dreamcast  ", 11) == 0;

    if (!valid) {
      LOG_INFO("bios_validate_flash resetting FLASH_PT_FACTORY");

      memcpy(sysinfo, "00000Dreamcast  ", sizeof(sysinfo));
      flash_erase_partition(flash, FLASH_PT_FACTORY);
      flash_write(flash, 0x1a000, sysinfo, sizeof(sysinfo));
      flash_write(flash, 0x1a0a0, sysinfo, sizeof(sysinfo));
    }
  }

  /* validate partition 1 (reserved) */
  {
    /* LOG_INFO("bios_validate_flash resetting FLASH_PT_RESERVED"); */

    flash_erase_partition(flash, FLASH_PT_RESERVED);
  }

  /* validate partition 2 (user settings, block allocated) */
  {
    if (!flash_check_header(flash, FLASH_PT_USER)) {
      LOG_INFO("bios_validate_flash resetting FLASH_PT_USER");

      flash_erase_partition(flash, FLASH_PT_USER);
      flash_write_header(flash, FLASH_PT_USER);
    }
  }

  /* validate partition 3 (game settings, block allocated) */
  {
    if (!flash_check_header(flash, FLASH_PT_GAME)) {
      LOG_INFO("bios_validate_flash resetting FLASH_PT_GAME");

      flash_erase_partition(flash, FLASH_PT_GAME);
      flash_write_header(flash, FLASH_PT_GAME);
    }
  }

  /* validate partition 4 (unknown, block allocated) */
  {
    if (!flash_check_header(flash, FLASH_PT_UNKNOWN)) {
      LOG_INFO("bios_validate_flash resetting FLASH_PT_UNKNOWN");

      flash_erase_partition(flash, FLASH_PT_UNKNOWN);
      flash_write_header(flash, FLASH_PT_UNKNOWN);
    }
  }
}

static int bios_post_init(struct device *dev) {
  struct bios *bios = (struct bios *)dev;

  bios_validate_flash(bios);

  bios_override_settings(bios);

#if 0
  /* this code enables a "hybrid" hle mode. in this mode, syscalls are patched
     to trap into their hle handlers, but the real bios can still be ran to
     test if bugs exist in the syscall emulation or bootstrap emulation. note,
     the boot rom does a bootstrap on startup which copies the boot rom into
     system ram. due to this, the invalid instructions are written to the
     original rom, not the system ram (or else, they would be overwritten by
     the bootstrap process) */
  struct boot *boot = bios->dc->boot;
  boot_rom_write(boot, SYSCALL_FONTROM - SH4_AREA3_BEGIN, 0x0, 0xffff);
  boot_rom_write(boot, SYSCALL_SYSINFO - SH4_AREA3_BEGIN, 0x0, 0xffff);
  boot_rom_write(boot, SYSCALL_FLASHROM - SH4_AREA3_BEGIN, 0x0, 0xffff);
  boot_rom_write(boot, SYSCALL_GDROM - SH4_AREA3_BEGIN, 0x0, 0xffff);
  boot_rom_write(boot, SYSCALL_GDROM2 - SH4_AREA3_BEGIN, 0x0, 0xffff);
  boot_rom_write(boot, SYSCALL_SYSTEM - SH4_AREA3_BEGIN, 0x0, 0xffff);
#endif

  return 1;
}

void bios_boot(struct bios *bios) {
  struct dreamcast *dc = bios->dc;
  struct flash *flash = dc->flash;
  struct gdrom *gd = dc->gdrom;
  struct sh4 *sh4 = dc->sh4;
  struct sh4_context *ctx = &sh4->ctx;

  const uint32_t BOOT1_ADDR = 0x8c008000;
  const uint32_t BOOT2_ADDR = 0x8c010000;
  const uint32_t SYSINFO_ADDR = 0x8c000068;

  LOG_INFO("bios_boot using hle bootstrap");

  if (!gdrom_get_disc(gd)) {
    LOG_FATAL("bios_boot failed, no disc is loaded");
    return;
  }

  /* load IP.BIN bootstrap */
  {
    /* bootstrap occupies the first 16 sectors of the data track */
    struct gd_session_info ses;
    gdrom_get_session(gd, 2, &ses);

    uint8_t tmp[DISC_MAX_SECTOR_SIZE * 16];
    int read = gdrom_read_sectors(gd, ses.fad, 16, GD_SECTOR_ANY, GD_MASK_DATA,
                                  tmp, sizeof(tmp));
    if (!read) {
      LOG_FATAL("bios_boot failed to copy IP.BIN");
      return;
    }

    sh4_memcpy_to_guest(dc->mem, BOOT1_ADDR, tmp, read);
  }

  /* load 1ST_READ.BIN into ram */
  {
    int fad, len;
    gdrom_get_bootfile(gd, &fad, &len);

    /* copy the bootfile into ram */
    uint8_t *tmp = malloc(len);
    int read = gdrom_read_bytes(gd, fad, len, tmp, len);
    if (read != len) {
      LOG_FATAL("bios_boot failed to copy bootfile");
      free(tmp);
      return;
    }

    /* CD-ROM XA discs have their binary scrambled. the bios descrambles this
       later on in the boot, but it should be fine to descramble now */
    struct gd_status_info stat;
    gdrom_get_status(gd, &stat);

    if (stat.format == GD_DISC_CDROM_XA) {
      uint8_t *tmp2 = malloc(len);
      descramble(tmp2, tmp, len);
      free(tmp);
      tmp = tmp2;
    }

    sh4_memcpy_to_guest(dc->mem, BOOT2_ADDR, tmp, read);
    free(tmp);
  }

  /* write system info */
  {
    uint8_t data[24] = {0};

    /* read system id from 0x0001a056 */
    flash_read(flash, 0x1a056, &data[0], 8);

    /* read system properties from 0x0001a000 */
    flash_read(flash, 0x1a000, &data[8], 5);

    /* read system settings */
    struct flash_syscfg_block syscfg;
    int r = flash_read_block(flash, FLASH_PT_USER, FLASH_USER_SYSCFG, &syscfg);
    CHECK_EQ(r, 1);

    memcpy(&data[16], &syscfg.time_lo, 8);

    sh4_memcpy_to_guest(dc->mem, SYSINFO_ADDR, data, sizeof(data));
  }

  /* write out syscall addresses to vectors */
  {
    sh4_write32(dc->mem, VECTOR_FONTROM, SYSCALL_FONTROM);
    sh4_write32(dc->mem, VECTOR_SYSINFO, SYSCALL_SYSINFO);
    sh4_write32(dc->mem, VECTOR_FLASHROM, SYSCALL_FLASHROM);
    sh4_write32(dc->mem, VECTOR_GDROM, SYSCALL_GDROM);
    sh4_write32(dc->mem, VECTOR_GDROM2, SYSCALL_GDROM2);
    sh4_write32(dc->mem, VECTOR_SYSTEM, SYSCALL_SYSTEM);
  }

  /* start executing at license screen code inside of IP.BIN */
  ctx->pc = 0xac008300;
}

int bios_invalid_instr(struct bios *bios) {
  struct dreamcast *dc = bios->dc;
  struct sh4_context *ctx = &dc->sh4->ctx;
  uint32_t pc = ctx->pc & 0x1cffffff;

  /* if an actual boot rom wasn't loaded into memory, a valid instruction won't
     exist at 0x0, causing an immediate trap on start */
  if (pc == 0x0) {
    bios_boot(bios);
    return 1;
  }

  int handled = 1;

  switch (pc) {
    case SYSCALL_FONTROM:
      bios_fontrom_vector(bios);
      break;

    case SYSCALL_SYSINFO:
      bios_sysinfo_vector(bios);
      break;

    case SYSCALL_FLASHROM:
      bios_flashrom_vector(bios);
      break;

    case SYSCALL_GDROM:
    case SYSCALL_GDROM2:
      bios_gdrom_vector(bios);
      break;

    case SYSCALL_SYSTEM:
      bios_system_vector(bios);
      break;

    default:
      handled = 0;
      break;
  }

  return handled;
}

void bios_destroy(struct bios *bios) {
  free(bios);
}

struct bios *bios_create(struct dreamcast *dc) {
  struct bios *bios =
      dc_create_device(dc, sizeof(struct bios), "bios", NULL, &bios_post_init);
  return bios;
}
