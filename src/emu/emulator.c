#include "emu/emulator.h"
#include "core/option.h"
#include "hw/dreamcast.h"
#include "hw/gdrom/gdrom.h"
#include "hw/memory.h"
#include "hw/scheduler.h"
#include "hw/sh4/sh4.h"
#include "sys/thread.h"
#include "sys/time.h"
#include "ui/nuklear.h"
#include "ui/window.h"

struct emu {
  struct window *window;
  struct window_listener listener;
  struct dreamcast *dc;
  int running;
  int throttled;
};

static bool emu_launch_bin(struct emu *emu, const char *path) {
  FILE *fp = fopen(path, "rb");
  if (!fp) {
    return false;
  }

  fseek(fp, 0, SEEK_END);
  int size = ftell(fp);
  fseek(fp, 0, SEEK_SET);

  // load to 0x0c010000 (area 3) which is where 1ST_READ.BIN is loaded to
  uint8_t *data = memory_translate(emu->dc->memory, "system ram", 0x00010000);
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

static void emu_paint(void *data) {
  struct emu *emu = data;

  dc_paint(emu->dc);
}

static void emu_paint_debug_menu(void *data, struct nk_context *ctx) {
  struct emu *emu = data;

  if (nk_tree_push(ctx, NK_TREE_TAB, "emu", NK_MINIMIZED)) {
    nk_checkbox_label(ctx, "throttled", &emu->throttled);
    nk_tree_pop(ctx);
  }

  dc_paint_debug_menu(emu->dc, ctx);
}

static void emu_keydown(void *data, enum keycode code, int16_t value) {
  struct emu *emu = data;

  if (code == K_F1) {
    if (value) {
      win_enable_debug_menu(emu->window, !emu->window->debug_menu);
    }
    return;
  }

  dc_keydown(emu->dc, code, value);
}

static void emu_close(void *data) {
  struct emu *emu = data;

  emu->running = 0;
}

static void *emu_core_thread(void *data) {
  struct emu *emu = data;

  static const int64_t MACHINE_STEP = HZ_TO_NANO(1000);
  int64_t current_time = time_nanoseconds();
  int64_t next_time = current_time;

  while (emu->running) {
    current_time = time_nanoseconds();

    int64_t delta_time = current_time - next_time;

    if (emu->throttled && delta_time < 0) {
      continue;
    }

    dc_tick(emu->dc, MACHINE_STEP);
    next_time = current_time + MACHINE_STEP;
  }

  return 0;
}

void emu_run(struct emu *emu, const char *path) {
  emu->dc = dc_create(emu->window->rb);

  if (!emu->dc) {
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

  // start core emulator thread
  thread_t core_thread;
  emu->running = 1;
  core_thread = thread_create(&emu_core_thread, NULL, emu);

  // run the renderer / ui in the main thread
  while (emu->running) {
    win_pump_events(emu->window);
  }

  // wait for the graphics thread to exit
  void *result;
  thread_join(core_thread, &result);
}

struct emu *emu_create(struct window *window) {
  struct emu *emu = calloc(1, sizeof(struct emu));

  emu->window = window;
  emu->listener = (struct window_listener){
      emu,        &emu_paint, &emu_paint_debug_menu, &emu_keydown, NULL, NULL,
      &emu_close, {0}};

  win_add_listener(emu->window, &emu->listener);

  return emu;
}

void emu_destroy(struct emu *emu) {
  win_remove_listener(emu->window, &emu->listener);

  if (emu->dc) {
    dc_destroy(emu->dc);
  }

  free(emu);
}
