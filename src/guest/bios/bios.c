#include "guest/bios/bios.h"
#include "core/core.h"
#include "core/time.h"
#include "guest/aica/aica.h"
#include "guest/bios/bios.h"
#include "guest/bios/flash.h"
#include "guest/bios/syscalls.h"
#include "guest/dreamcast.h"
#include "guest/gdrom/gdrom.h"
#include "guest/rom/flash.h"
#include "guest/sh4/sh4.h"
#include "imgui.h"
#include "options.h"

/* system settings */
static const char *regions[] = {
    "japan", "usa", "europe",
};

static const char *languages[] = {
    "japanese", "english", "german", "french", "spanish", "italian",
};

static const char *broadcasts[] = {
    "ntsc", "pal", "pal_m", "pal_n",
};

/* address of syscall vectors */
enum {
  VECTOR_SYSINFO = 0x0c0000b0,
  VECTOR_FONTROM = 0x0c0000b4,
  VECTOR_FLASHROM = 0x0c0000b8,
  VECTOR_GDROM = 0x0c0000bc,
  VECTOR_MENU = 0x0c0000e0,
};

/* address of syscall entrypoints */
enum {
  SYSCALL_SYSINFO = 0x0c003c00,
  SYSCALL_FONTROM = 0x0c003b80,
  SYSCALL_FLASHROM = 0x0c003d00,
  SYSCALL_GDROM = 0x0c001000,
  SYSCALL_MENU = 0x0c000800,
};

static uint32_t bios_local_time() {
  /* dreamcast system time is relative to 1/1/1950 00:00 */
  struct tm timeinfo;
  timeinfo.tm_year = 50;
  timeinfo.tm_mon = 0;
  timeinfo.tm_mday = 1;
  timeinfo.tm_hour = 0;
  timeinfo.tm_min = 0;
  timeinfo.tm_sec = 0;

  time_t base_time = mktime(&timeinfo);
  time_t curr_time = time(NULL);
  double delta = difftime(curr_time, base_time);
  return (uint32_t)delta;
}

static void bios_override_settings(struct bios *bios) {
  struct dreamcast *dc = bios->dc;
  struct flash *flash = dc->flash;
  struct gdrom *gd = dc->gdrom;

  int region = 0;
  int lang = 0;
  int bcast = 0;
  uint32_t time = bios_local_time();

  for (int i = 0; i < ARRAY_SIZE(regions); i++) {
    if (!strcmp(OPTION_region, regions[i])) {
      region = i;
      break;
    }
  }

  for (int i = 0; i < ARRAY_SIZE(languages); i++) {
    if (!strcmp(OPTION_language, languages[i])) {
      lang = i;
      break;
    }
  }

  for (int i = 0; i < ARRAY_SIZE(broadcasts); i++) {
    if (!strcmp(OPTION_broadcast, broadcasts[i])) {
      bcast = i;
      break;
    }
  }

  LOG_INFO("bios_override_settings region=%s lang=%s bcast=%s", regions[region],
           languages[lang], broadcasts[bcast]);

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

static int bios_boot(struct bios *bios) {
  struct dreamcast *dc = bios->dc;
  struct flash *flash = dc->flash;
  struct gdrom *gd = dc->gdrom;
  struct sh4 *sh4 = dc->sh4;
  struct sh4_context *ctx = &sh4->ctx;
  struct address_space *space = sh4->memory_if->space;

  const uint32_t BOOT1_ADDR = 0x8c008000;
  const uint32_t BOOT2_ADDR = 0x8c010000;
  const uint32_t SYSINFO_ADDR = 0x8c000068;

  LOG_INFO("bios_boot using hle bootstrap");

  if (!gdrom_has_disc(gd)) {
    LOG_WARNING("bios_boot failed, no disc is loaded");
    return 0;
  }

  /* load IP.BIN bootstrap */
  {
    /* bootstrap occupies the first 16 sectors of the data track */
    struct gd_spi_session data_session;
    gdrom_get_session(gd, 2, &data_session);

    uint8_t tmp[DISC_MAX_SECTOR_SIZE * 16];
    int read = gdrom_read_sectors(gd, data_session.fad, 16, GD_SECTOR_ANY,
                                  GD_MASK_DATA, tmp, sizeof(tmp));
    if (!read) {
      LOG_WARNING("bios_boot failed to copy IP.BIN");
      return 0;
    }

    as_memcpy_to_guest(space, BOOT1_ADDR, tmp, read);
  }

  /* load 1ST_READ.BIN into ram */
  {
    int fad, len;
    gdrom_get_bootfile(gd, &fad, &len);

    /* copy the bootfile into ram */
    uint8_t *tmp = malloc(len);
    int read = gdrom_read_bytes(gd, fad, len, tmp, len);
    if (read != len) {
      LOG_WARNING("bios_boot failed to copy bootfile");
      free(tmp);
      return 0;
    }

    as_memcpy_to_guest(space, BOOT2_ADDR, tmp, read);
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

    as_memcpy_to_guest(space, SYSINFO_ADDR, data, sizeof(data));
  }

  /* write out syscall addresses to vectors */
  {
    as_write32(space, VECTOR_FONTROM, SYSCALL_FONTROM);
    as_write32(space, VECTOR_SYSINFO, SYSCALL_SYSINFO);
    as_write32(space, VECTOR_FLASHROM, SYSCALL_FLASHROM);
    as_write32(space, VECTOR_GDROM, SYSCALL_GDROM);
    as_write32(space, VECTOR_MENU, SYSCALL_MENU);
  }

  /* start executing at license screen code inside of ip.bin */
  ctx->pc = 0xac008300;

  return 1;
}

static int bios_post_init(struct device *dev) {
  struct bios *bios = (struct bios *)dev;

  bios_validate_flash(bios);

  bios_override_settings(bios);

/* this code enables a "hybrid" hle mode. in this mode, syscalls are patched
   to trap into their hle handlers, but the real bios can still be ran to
   test if bugs exist in the syscall emulation or bootstrap emulation */
#if 0
  /* write out invalid instructions at syscall entry points. note, the boot rom
     does a bootstrap on startup which copies the boot rom into system ram. due
     to this, the invalid instructions are written to the original rom, not the
     system ram (or else, they would be overwritten by the bootstrap process) */
  struct boot *boot = bios->dc->boot;
  uint16_t invalid = 0x0;

  boot_write(boot, SYSCALL_FONTROM, &invalid, 2);
  boot_write(boot, SYSCALL_SYSINFO, &invalid, 2);
  boot_write(boot, SYSCALL_FLASHROM, &invalid, 2);
  boot_write(boot, SYSCALL_GDROM, &invalid, 2);
  /*boot_write(boot, SYSCALL_MENU, &invalid, 2);*/
#endif

  return 1;
}

#ifdef HAVE_IMGUI
void bios_debug_menu(struct bios *bios) {
  int changed = 0;

  if (igBeginMainMenuBar()) {
    if (igBeginMenu("BIOS", 1)) {
      if (igBeginMenu("region", 1)) {
        for (int i = 0; i < ARRAY_SIZE(regions); i++) {
          const char *region = regions[i];
          int selected = !strcmp(OPTION_region, region);

          if (igMenuItem(region, NULL, selected, 1)) {
            changed = 1;
            strncpy(OPTION_region, region, sizeof(OPTION_region));
          }
        }
        igEndMenu();
      }

      if (igBeginMenu("language", 1)) {
        for (int i = 0; i < ARRAY_SIZE(languages); i++) {
          const char *language = languages[i];
          int selected = !strcmp(OPTION_language, language);

          if (igMenuItem(language, NULL, selected, 1)) {
            changed = 1;
            strncpy(OPTION_language, language, sizeof(OPTION_language));
          }
        }
        igEndMenu();
      }

      if (igBeginMenu("broadcast", 1)) {
        for (int i = 0; i < ARRAY_SIZE(broadcasts); i++) {
          const char *broadcast = broadcasts[i];
          int selected = !strcmp(OPTION_broadcast, broadcast);

          if (igMenuItem(broadcast, NULL, selected, 1)) {
            changed = 1;
            strncpy(OPTION_broadcast, broadcast, sizeof(OPTION_broadcast));
          }
        }
        igEndMenu();
      }

      igEndMenu();
    }
    igEndMainMenuBar();
  }

  if (changed) {
    LOG_WARNING("bios settings changed, restart to apply");
  }
}
#endif

int bios_invalid_instr(struct bios *bios) {
  struct dreamcast *dc = bios->dc;
  struct sh4_context *ctx = &dc->sh4->ctx;
  uint32_t pc = ctx->pc & 0x1cffffff;

  /* if an actual boot rom wasn't loaded into memory, a valid instruction won't
     exist at 0x0, causing an immediate trap on start */
  if (pc == 0x0) {
    return bios_boot(bios);
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
      bios_gdrom_vector(bios);
      break;

    case SYSCALL_MENU:
      bios_menu_vector(bios);
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
