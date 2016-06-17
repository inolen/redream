#include "core/option.h"
#include "emu/emulator.h"
#include "hw/gdrom/gdrom.h"
#include "hw/sh4/sh4.h"
#include "hw/dreamcast.h"
#include "hw/memory.h"
#include "hw/scheduler.h"
#include "ui/window.h"
#include "sys/time.h"

DEFINE_OPTION_STRING(bios, "/Users/inolen/projects/dreamcast/dc_boot.bin",
                     "Path to BIOS");
DEFINE_OPTION_STRING(flash, "/Users/inolen/projects/dreamcast/dc_flash.bin",
                     "Path to flash ROM");

typedef struct emu_s {
  struct window *window;
  struct window_listener *listener;
  struct dreamcast *dc;
  bool running;
} emu_t;

static bool emu_load_bios(emu_t *emu, const char *path) {
  static const int BIOS_BEGIN = 0x00000000;
  static const int BIOS_SIZE = 0x00200000;

  FILE *fp = fopen(path, "rb");
  if (!fp) {
    LOG_WARNING("Failed to open bios at \"%s\"", path);
    return false;
  }

  fseek(fp, 0, SEEK_END);
  int size = ftell(fp);
  fseek(fp, 0, SEEK_SET);

  if (size != BIOS_SIZE) {
    LOG_WARNING("Bios size mismatch, is %d, expected %d", size, BIOS_SIZE);
    fclose(fp);
    return false;
  }

  uint8_t *bios = as_translate(emu->dc->sh4->base.memory->space, BIOS_BEGIN);
  int n = (int)fread(bios, sizeof(uint8_t), size, fp);
  fclose(fp);

  if (n != size) {
    LOG_WARNING("Bios read failed");
    return false;
  }

  return true;
}

static bool emu_load_flash(emu_t *emu, const char *path) {
  static const int FLASH_BEGIN = 0x00200000;
  static const int FLASH_SIZE = 0x00020000;

  FILE *fp = fopen(path, "rb");
  if (!fp) {
    LOG_WARNING("Failed to open flash at \"%s\"", path);
    return false;
  }

  fseek(fp, 0, SEEK_END);
  int size = ftell(fp);
  fseek(fp, 0, SEEK_SET);

  if (size != FLASH_SIZE) {
    LOG_WARNING("Flash size mismatch, is %d, expected %d", size, FLASH_SIZE);
    fclose(fp);
    return false;
  }

  uint8_t *flash = as_translate(emu->dc->sh4->base.memory->space, FLASH_BEGIN);
  int n = (int)fread(flash, sizeof(uint8_t), size, fp);
  fclose(fp);

  if (n != size) {
    LOG_WARNING("Flash read failed");
    return false;
  }

  return true;
}

static bool emu_launch_bin(emu_t *emu, const char *path) {
  FILE *fp = fopen(path, "rb");
  if (!fp) {
    return false;
  }

  fseek(fp, 0, SEEK_END);
  int size = ftell(fp);
  fseek(fp, 0, SEEK_SET);

  // load to 0x0c010000 (area 3) which is where 1ST_READ.BIN is loaded to
  uint8_t *data = as_translate(emu->dc->sh4->base.memory->space, 0x0c010000);
  int n = (int)fread(data, sizeof(uint8_t), size, fp);
  fclose(fp);

  if (n != size) {
    LOG_WARNING("BIN read failed");
    return false;
  }

  gdrom_set_disc(emu->dc->gdrom, NULL);
  sh4_set_pc(emu->dc->sh4, 0x0c010000);

  return true;
}

static bool emu_launch_gdi(emu_t *emu, const char *path) {
  struct disc *disc = disc_create_gdi(path);

  if (!disc) {
    return false;
  }

  gdrom_set_disc(emu->dc->gdrom, disc);
  sh4_set_pc(emu->dc->sh4, 0xa0000000);

  return true;
}

static void emu_onpaint(void *data, bool show_main_menu) {
  emu_t *emu = data;

  dc_paint(emu->dc, show_main_menu);
}

static void emu_onkeydown(void *data, enum keycode code, int16_t value) {
  emu_t *emu = data;

  if (code == K_F1) {
    if (value) {
      win_enable_main_menu(emu->window, !win_main_menu_enabled(emu->window));
    }
    return;
  }

  dc_keydown(emu->dc, code, value);
}

static void emu_onclose(void *data) {
  emu_t *emu = data;

  emu->running = false;
}

void emu_run(emu_t *emu, const char *path) {
  emu->dc = dc_create(win_render_backend(emu->window));

  if (!emu->dc) {
    return;
  }

  if (!emu_load_bios(emu, OPTION_bios)) {
    return;
  }

  if (!emu_load_flash(emu, OPTION_flash)) {
    return;
  }

  if (path) {
    LOG_INFO("Launching %s", path);

    if ((strstr(path, ".bin") && !emu_launch_bin(emu, path)) ||
        (strstr(path, ".gdi") && !emu_launch_gdi(emu, path))) {
      LOG_WARNING("Failed to launch %s", path);
      return;
    }
  }

  // start running
  static const int64_t MACHINE_STEP = HZ_TO_NANO(1000);
  static const int64_t FRAME_STEP = HZ_TO_NANO(60);

  int64_t current_time = time_nanoseconds();
  int64_t last_time = current_time;

  int64_t next_machine_time = current_time;
  int64_t next_frame_time = current_time;

  emu->running = true;

  while (emu->running) {
    current_time = time_nanoseconds();
    last_time = current_time;

    // run dreamcast machine
    if (current_time > next_machine_time) {
      dc_tick(emu->dc, MACHINE_STEP);

      next_machine_time = current_time + MACHINE_STEP;
    }

    // run local frame
    if (current_time > next_frame_time) {
      win_pump_events(emu->window);

      next_frame_time = current_time + FRAME_STEP;
    }
  }
}

emu_t *emu_create(struct window *window) {
  static const struct window_callbacks callbacks = {
      NULL, &emu_onpaint, NULL, &emu_onkeydown, NULL, NULL, &emu_onclose};

  emu_t *emu = calloc(1, sizeof(emu_t));

  emu->window = window;
  emu->listener = win_add_listener(emu->window, &callbacks, emu);

  return emu;
}

void emu_destroy(emu_t *emu) {
  win_remove_listener(emu->window, emu->listener);

  if (emu->dc) {
    dc_destroy(emu->dc);
  }

  free(emu);
}
