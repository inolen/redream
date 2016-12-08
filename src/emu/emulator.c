#include "emu/emulator.h"
#include "core/option.h"
#include "core/profiler.h"
#include "hw/arm7/arm7.h"
#include "hw/dreamcast.h"
#include "hw/gdrom/gdrom.h"
#include "hw/memory.h"
#include "hw/pvr/pvr.h"
#include "hw/pvr/ta.h"
#include "hw/pvr/tr.h"
#include "hw/scheduler.h"
#include "hw/sh4/sh4.h"
#include "sys/thread.h"
#include "sys/time.h"
#include "ui/nuklear.h"
#include "ui/window.h"

DEFINE_AGGREGATE_COUNTER(frames);

struct emu {
  struct window *window;
  struct window_listener listener;
  struct dreamcast *dc;
  int running;
  int throttled;

  /* render state */
  struct tr *tr;
  struct render_context render_ctx;
  struct surface surfs[TA_MAX_SURFS];
  struct vertex verts[TA_MAX_VERTS];
  int sorted_surfs[TA_MAX_SURFS];
};

static bool emu_launch_bin(struct emu *emu, const char *path) {
  FILE *fp = fopen(path, "rb");
  if (!fp) {
    return false;
  }

  fseek(fp, 0, SEEK_END);
  int size = ftell(fp);
  fseek(fp, 0, SEEK_SET);

  /* load to 0x0c010000 (area 3) which is where 1ST_READ.BIN is loaded to */
  uint8_t *data = memory_translate(emu->dc->memory, "system ram", 0x00010000);
  int n = (int)fread(data, sizeof(uint8_t), size, fp);
  fclose(fp);

  if (n != size) {
    LOG_WARNING("BIN read failed");
    return false;
  }

  gdrom_set_disc(emu->dc->gdrom, NULL);
  sh4_reset(emu->dc->sh4, 0x0c010000);
  dc_resume(emu->dc);

  return true;
}

static bool emu_launch_gdi(struct emu *emu, const char *path) {
  struct disc *disc = disc_create_gdi(path);

  if (!disc) {
    return false;
  }

  gdrom_set_disc(emu->dc->gdrom, disc);
  sh4_reset(emu->dc->sh4, 0xa0000000);
  dc_resume(emu->dc);

  return true;
}

static void emu_paint(void *data) {
  struct emu *emu = data;

  /* render latest ta context */
  struct render_context *render_ctx = &emu->render_ctx;
  struct tile_ctx *pending_ctx = NULL;
  int pending_frame = 0;

  if (ta_lock_pending_context(emu->dc->ta, &pending_ctx, &pending_frame)) {
    render_ctx->surfs = emu->surfs;
    render_ctx->surfs_size = array_size(emu->surfs);
    render_ctx->verts = emu->verts;
    render_ctx->verts_size = array_size(emu->verts);
    render_ctx->sorted_surfs = emu->sorted_surfs;
    render_ctx->sorted_surfs_size = array_size(emu->sorted_surfs);
    tr_parse_context(emu->tr, pending_ctx, pending_frame, render_ctx);

    ta_unlock_pending_context(emu->dc->ta);
  }

  tr_render_context(emu->tr, render_ctx);
  prof_counter_add(COUNTER_frames, 1);

  prof_flip();
}

static void emu_debug_menu(void *data, struct nk_context *ctx) {
  struct emu *emu = data;

  /* set status string */
  char status[128];

  int frames = (int)prof_counter_load(COUNTER_frames);
  int pvr_vblanks = (int)prof_counter_load(COUNTER_pvr_vblanks);
  int sh4_instrs = (int)(prof_counter_load(COUNTER_sh4_instrs) / 1000000.0f);
  int arm7_instrs = (int)(prof_counter_load(COUNTER_arm7_instrs) / 1000000.0f);

  snprintf(status, sizeof(status), "%3d FPS %3d VBS %4d SH4 %d ARM", frames,
           pvr_vblanks, sh4_instrs, arm7_instrs);
  win_set_status(emu->window, status);

  /* add drop down menus */
  nk_layout_row_push(ctx, 70.0f);
  if (nk_menu_begin_label(ctx, "EMULATOR", NK_TEXT_LEFT,
                          nk_vec2(140.0f, 200.0f))) {
    nk_layout_row_dynamic(ctx, DEBUG_MENU_HEIGHT, 1);
    nk_checkbox_label(ctx, "throttled", &emu->throttled);
    nk_menu_end(ctx);
  }

  dc_debug_menu(emu->dc, ctx);
}

static void emu_keydown(void *data, int device_index, enum keycode code,
                        int16_t value) {
  struct emu *emu = data;

  if (code == K_F1) {
    if (value) {
      win_enable_debug_menu(emu->window, !emu->window->debug_menu);
    }
    return;
  }

  dc_keydown(emu->dc, device_index, code, value);
}

static void emu_joy_add(void *data, int joystick_index) {
  struct emu *emu = data;

  dc_joy_add(emu->dc, joystick_index);
}

static void emu_joy_remove(void *data, int joystick_index) {
  struct emu *emu = data;

  dc_joy_remove(emu->dc, joystick_index);
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

    if (!emu->throttled || delta_time >= 0) {
      dc_tick(emu->dc, MACHINE_STEP);
      next_time = current_time + MACHINE_STEP;
    }
  }

  return 0;
}

void emu_run(struct emu *emu, const char *path) {
  emu->dc = dc_create();

  if (!emu->dc) {
    return;
  }

  /* create tile renderer */
  emu->tr = tr_create(emu->window->rb, ta_texture_provider(emu->dc->ta));

  /* load gdi / bin if specified */
  if (path) {
    LOG_INFO("Launching %s", path);

    if ((strstr(path, ".bin") && !emu_launch_bin(emu, path)) ||
        (strstr(path, ".gdi") && !emu_launch_gdi(emu, path))) {
      LOG_WARNING("Failed to launch %s", path);
      return;
    }
  }
  /* else, boot to main menu */
  else {
    sh4_reset(emu->dc->sh4, 0xa0000000);
    dc_resume(emu->dc);
  }

  /* start core emulator thread */
  thread_t core_thread;
  emu->running = 1;
  core_thread = thread_create(&emu_core_thread, NULL, emu);

  /* run the renderer / ui in the main thread */
  while (emu->running) {
    win_pump_events(emu->window);
  }

  /* wait for the graphics thread to exit */
  void *result;
  thread_join(core_thread, &result);
}

void emu_destroy(struct emu *emu) {
  if (emu->tr) {
    tr_destroy(emu->tr);
  }
  if (emu->dc) {
    dc_destroy(emu->dc);
  }
  win_remove_listener(emu->window, &emu->listener);
  free(emu);
}

struct emu *emu_create(struct window *window) {
  struct emu *emu = calloc(1, sizeof(struct emu));

  emu->window = window;
  emu->listener =
      (struct window_listener){emu,                  &emu_paint,            &emu_debug_menu,
                               &emu_joy_add,         &emu_joy_remove,       &emu_keydown,
                               NULL,                 NULL,                  &emu_close,
                               {0}};
  win_add_listener(emu->window, &emu->listener);

  return emu;
}
