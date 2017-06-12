#include <time.h>
#include "bios/bios.h"
#include "bios/flash.h"
#include "core/math.h"
#include "core/option.h"
#include "dreamcast.h"
#include "hw/aica/aica.h"
#include "hw/rom/flash.h"
#include "render/imgui.h"

DEFINE_OPTION_STRING(region, "america", "System region");
DEFINE_OPTION_STRING(language, "english", "System language");
DEFINE_OPTION_STRING(broadcast, "ntsc", "System broadcast mode");

static const char *regions[] = {
    "japan", "america", "europe",
};

static const char *languages[] = {
    "japanese", "english", "german", "french", "spanish", "italian",
};

static const char *broadcasts[] = {
    "ntsc", "pal", "pal_m", "pal_n",
};

struct bios {
  struct dreamcast *dc;
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

static void bios_override_flash_settings(struct bios *bios) {
  struct dreamcast *dc = bios->dc;
  struct flash *flash = dc->flash;

  int region = 0;
  int lang = 0;
  int bcast = 0;
  uint32_t time = bios_local_time();

  for (int i = 0; i < array_size(regions); i++) {
    if (!strcmp(OPTION_region, regions[i])) {
      region = i;
      break;
    }
  }

  for (int i = 0; i < array_size(languages); i++) {
    if (!strcmp(OPTION_language, languages[i])) {
      lang = i;
      break;
    }
  }

  for (int i = 0; i < array_size(broadcasts); i++) {
    if (!strcmp(OPTION_broadcast, broadcasts[i])) {
      bcast = i;
      break;
    }
  }

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
  CHECK_EQ(res, 1);

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
  struct flash_header_block header;

  /* validate partition 0 (factory settings) */
  char sysinfo[2][16];
  flash_read(flash, 0x1a000, sysinfo[0], sizeof(sysinfo[0]));
  flash_read(flash, 0x1a0a0, sysinfo[1], sizeof(sysinfo[1]));

  /* write out default sysinfo if missing */
  if (memcmp(&sysinfo[0][5], "Dreamcast  ", 11) != 0 ||
      memcmp(&sysinfo[1][5], "Dreamcast  ", 11) != 0) {
    memcpy(sysinfo[0], "00000Dreamcast  ", sizeof(sysinfo[0]));
    flash_write(flash, 0x1a000, sysinfo[0], sizeof(sysinfo[0]));
    flash_write(flash, 0x1a0a0, sysinfo[0], sizeof(sysinfo[0]));
  }

  /* validate partition 1 (reserved) */
  flash_erase_partition(flash, FLASH_PT_RESERVED);

  /* validate partition 2 (user settings, block allocated) */
  if (!flash_read_block(flash, FLASH_PT_USER, 0, &header)) {
    flash_erase_partition(flash, FLASH_PT_USER);

    /* write out default user settings */
    struct flash_syscfg_block syscfg;
    memset(&syscfg, 0xff, sizeof(syscfg));
    syscfg.time_lo = 0;
    syscfg.time_hi = 0;
    syscfg.lang = 0;
    syscfg.mono = 1;
    syscfg.autostart = 1;

    int res =
        flash_write_block(flash, FLASH_PT_USER, FLASH_USER_SYSCFG, &syscfg);
    CHECK_EQ(res, 1);
  }

  /* validate partition 3 (game settings, block allocated) */
  if (!flash_read_block(flash, FLASH_PT_GAME, 0, &header)) {
    flash_erase_partition(flash, FLASH_PT_GAME);
  }

  /* validate partition 4 (unknown, block allocated) */
  if (!flash_read_block(flash, FLASH_PT_UNKNOWN, 0, &header)) {
    flash_erase_partition(flash, FLASH_PT_UNKNOWN);
  }
}

void bios_debug_menu(struct bios *bios) {
  int changed = 0;

  if (igBeginMainMenuBar()) {
    if (igBeginMenu("BIOS", 1)) {
      if (igBeginMenu("region", 1)) {
        for (int i = 0; i < array_size(regions); i++) {
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
        for (int i = 0; i < array_size(languages); i++) {
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
        for (int i = 0; i < array_size(broadcasts); i++) {
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
    LOG_WARNING("bios settings changed, restart for changes to take effect");
  }
}

int bios_init(struct bios *bios) {
  bios_validate_flash(bios);

  bios_override_flash_settings(bios);

  return 1;
}

void bios_destroy(struct bios *bios) {
  free(bios);
}

struct bios *bios_create(struct dreamcast *dc) {
  struct bios *bios = calloc(1, sizeof(struct bios));

  bios->dc = dc;

  return bios;
}
