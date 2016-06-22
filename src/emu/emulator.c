#include "core/option.h"
#include "emu/emulator.h"
#include "hw/gdrom/gdrom.h"
#include "hw/sh4/sh4.h"
#include "hw/dreamcast.h"
#include "hw/scheduler.h"
#include "hw/memory.h"
#include "ui/nuklear.h"
#include "ui/window.h"
#include "sys/time.h"

DEFINE_OPTION_STRING(bios, "dc_boot.bin", "Path to BIOS");
DEFINE_OPTION_STRING(flash, "dc_flash.bin", "Path to flash ROM");

struct emu {
  struct window *window;
  struct window_listener *listener;
  struct dreamcast *dc;
  bool running;
};

static bool emu_load_bios(struct emu *emu, const char *path) {
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

static bool emu_load_flash(struct emu *emu, const char *path) {
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

static bool emu_launch_bin(struct emu *emu, const char *path) {
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

static bool emu_launch_gdi(struct emu *emu, const char *path) {
  struct disc *disc = disc_create_gdi(path);

  if (!disc) {
    return false;
  }

  gdrom_set_disc(emu->dc->gdrom, disc);
  sh4_set_pc(emu->dc->sh4, 0xa0000000);

  return true;
}

static void emu_onpaint(void *data, bool show_main_menu) {
  struct emu *emu = data;

  dc_paint(emu->dc, show_main_menu);

  {
    struct nk_context *ctx = &emu->window->nk->ctx;
    struct nk_color background;
    struct nk_panel layout;

    if (nk_begin(ctx, &layout, "Demo", nk_rect(200, 200, 210, 250),
                 NK_WINDOW_BORDER | NK_WINDOW_MOVABLE | NK_WINDOW_SCALABLE | NK_WINDOW_MINIMIZABLE | NK_WINDOW_TITLE)) {
      enum { EASY, HARD };
      static int op = EASY;
      static int property = 20;

      nk_layout_row_static(ctx, 30, 80, 1);
      if (nk_button_label(ctx, "button", NK_BUTTON_DEFAULT)) {
        fprintf(stdout, "button pressed\n");
      }
      nk_layout_row_dynamic(ctx, 30, 2);
      if (nk_option_label(ctx, "easy", op == EASY)) {
        op = EASY;
      }
      if (nk_option_label(ctx, "hard", op == HARD)) {
        op = HARD;
      }
      nk_layout_row_dynamic(ctx, 25, 1);
      nk_property_int(ctx, "Compression:", 0, &property, 100, 10, 1);

      {
        struct nk_panel combo;
        nk_layout_row_dynamic(ctx, 20, 1);
        nk_label(ctx, "background:", NK_TEXT_LEFT);
        nk_layout_row_dynamic(ctx, 25, 1);
        if (nk_combo_begin_color(ctx, &combo, background, 400)) {
          nk_layout_row_dynamic(ctx, 120, 1);
          background = nk_color_picker(ctx, background, NK_RGBA);
          nk_layout_row_dynamic(ctx, 25, 1);
          background.r = (nk_byte)nk_propertyi(ctx, "#R:", 0, background.r, 255, 1, 1);
          background.g = (nk_byte)nk_propertyi(ctx, "#G:", 0, background.g, 255, 1, 1);
          background.b = (nk_byte)nk_propertyi(ctx, "#B:", 0, background.b, 255, 1, 1);
          background.a = (nk_byte)nk_propertyi(ctx, "#A:", 0, background.a, 255, 1, 1);
          nk_combo_end(ctx);
        }
      }
    }

    nk_end(ctx);
  }
}

static void emu_onkeydown(void *data, enum keycode code, int16_t value) {
  struct emu *emu = data;

  if (code == K_F1) {
    if (value) {
      win_enable_main_menu(emu->window, !emu->window->main_menu);
    }
    return;
  }

  dc_keydown(emu->dc, code, value);
}

static void emu_onclose(void *data) {
  struct emu *emu = data;

  emu->running = false;
}

void emu_run(struct emu *emu, const char *path) {
  emu->dc = dc_create(emu->window->rb);

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
  int64_t next_machine_time = current_time;
  int64_t next_frame_time = current_time;

  emu->running = true;

  while (emu->running) {
    current_time = time_nanoseconds();

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

struct emu *emu_create(struct window *window) {
  static const struct window_callbacks callbacks = {
      NULL, &emu_onpaint, NULL, &emu_onkeydown, NULL, NULL, &emu_onclose};

  struct emu *emu = calloc(1, sizeof(struct emu));

  emu->window = window;
  emu->listener = win_add_listener(emu->window, &callbacks, emu);

  return emu;
}

void emu_destroy(struct emu *emu) {
  win_remove_listener(emu->window, emu->listener);

  if (emu->dc) {
    dc_destroy(emu->dc);
  }

  free(emu);
}
