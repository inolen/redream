#include "emulator.h"
#include "audio/audio_backend.h"
#include "core/option.h"
#include "core/profiler.h"
#include "hw/aica/aica.h"
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
#include "ui/microprofile.h"
#include "ui/nuklear.h"
#include "ui/window.h"
#include "video/render_backend.h"

DEFINE_AGGREGATE_COUNTER(frames);

DEFINE_OPTION_INT(audio, 1, "Enable audio");

struct emu {
  struct window *window;
  struct window_listener listener;
  struct dreamcast *dc;
  volatile int running;

  int debug_menu;

  struct render_backend *rb;
  struct microprofile *mp;
  struct nuklear *nk;

  /* render state */
  struct tr *tr;
  struct render_context rc;
};

static int emu_launch_bin(struct emu *emu, const char *path) {
  FILE *fp = fopen(path, "rb");
  if (!fp) {
    return 0;
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
    return 0;
  }

  sh4_reset(emu->dc->sh4, 0x0c010000);
  dc_resume(emu->dc);

  return 1;
}

static int emu_launch_gdi(struct emu *emu, const char *path) {
  struct disc *disc = disc_create_gdi(path);

  if (!disc) {
    return 0;
  }

  gdrom_set_disc(emu->dc->gdrom, disc);
  sh4_reset(emu->dc->sh4, 0xa0000000);
  dc_resume(emu->dc);

  return 1;
}

static void emu_paint(struct emu *emu) {
  rb_begin_frame(emu->rb);
  nk_begin_frame(emu->nk);
  mp_begin_frame(emu->mp);

  /* render the next ta context */
  {
    struct render_context *rc = &emu->rc;
    struct tile_ctx *pending_ctx = NULL;

    while (emu->running) {
      if (ta_lock_pending_context(emu->dc->ta, &pending_ctx, 1000)) {
        tr_parse_context(emu->tr, pending_ctx, rc);
        ta_unlock_pending_context(emu->dc->ta);
        break;
      }
    }

    tr_render_context(emu->tr, rc);
  }

  /* render debug menus */
  {
    if (emu->debug_menu) {
      struct nk_context *ctx = &emu->nk->ctx;
      struct nk_rect bounds = {0.0f, 0.0f, (float)emu->window->width,
                               DEBUG_MENU_HEIGHT};

      nk_style_default(ctx);

      ctx->style.window.border = 0.0f;
      ctx->style.window.menu_border = 0.0f;
      ctx->style.window.spacing = nk_vec2(0.0f, 0.0f);
      ctx->style.window.padding = nk_vec2(0.0f, 0.0f);

      if (nk_begin(ctx, "debug menu", bounds, NK_WINDOW_NO_SCROLLBAR)) {
        nk_menubar_begin(ctx);
        nk_layout_row_begin(ctx, NK_STATIC, DEBUG_MENU_HEIGHT,
                            MAX_WINDOW_LISTENERS + 2);

        /* add our own debug menu */
        nk_layout_row_push(ctx, 30.0f);
        if (nk_menu_begin_label(ctx, "EMU", NK_TEXT_LEFT,
                                nk_vec2(140.0f, 200.0f))) {
          nk_layout_row_dynamic(ctx, DEBUG_MENU_HEIGHT, 1);

          int fullscreen = emu->window->fullscreen;
          if (nk_checkbox_label(ctx, "fullscreen", &fullscreen)) {
            win_set_fullscreen(emu->window, fullscreen);
          }

          nk_menu_end(ctx);
        }

        /* add each devices's debug menu */
        dc_debug_menu(emu->dc, ctx);

        /* fill up remaining space with status */
        char status[128];

        int frames = (int)prof_counter_load(COUNTER_frames);
        int ta_renders = (int)prof_counter_load(COUNTER_ta_renders);
        int pvr_vblanks = (int)prof_counter_load(COUNTER_pvr_vblanks);
        int sh4_instrs =
            (int)(prof_counter_load(COUNTER_sh4_instrs) / 1000000.0f);
        int arm7_instrs =
            (int)(prof_counter_load(COUNTER_arm7_instrs) / 1000000.0f);

        snprintf(status, sizeof(status),
                 "FPS %3d RPS %3d VBS %3d SH4 %4d ARM %d", frames, ta_renders,
                 pvr_vblanks, sh4_instrs, arm7_instrs);

        nk_layout_row_push(ctx, (float)emu->window->width -
                                    ctx->current->layout->row.item_offset);
        nk_label(ctx, status, NK_TEXT_RIGHT);

        nk_layout_row_end(ctx);
        nk_menubar_end(ctx);
      }
      nk_end(ctx);
    }
  }

  /* update profiler stats */
  prof_counter_add(COUNTER_frames, 1);
  prof_flip();

  mp_end_frame(emu->mp);
  nk_end_frame(emu->nk);
  rb_end_frame(emu->rb);
}

static void emu_keydown(void *data, int device_index, enum keycode code,
                        int16_t value) {
  struct emu *emu = data;

  if (code == K_F1) {
    if (value > 0) {
      emu->debug_menu = emu->debug_menu ? 0 : 1;
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
  struct audio_backend *audio = NULL;

  if (OPTION_audio) {
    audio = audio_create(emu->dc->aica);

    if (!audio) {
      LOG_WARNING("Audio backend creation failed");
      goto exit;
    }
  }

  static const int64_t MACHINE_STEP = HZ_TO_NANO(1000);
  int64_t current_time = 0;
  int64_t next_pump_time = 0;

  while (emu->running) {
    /* run a slice of dreamcast time if the available audio is running low. this
       effectively synchronizes the emulation speed with the host audio clock.
       note however, if audio is disabled, the emulator will run as fast as
       possible */
    if (!audio || audio_buffer_low(audio)) {
      dc_tick(emu->dc, MACHINE_STEP);
    }

    /* update profiler stats */
    current_time = time_nanoseconds();
    prof_update(current_time);

    /* check audio events (device connect / disconnect, etc.) infrequently */
    if (audio && current_time > next_pump_time) {
      audio_pump_events(audio);
      next_pump_time = current_time + NS_PER_SEC;
    }
  }

exit:
  if (audio) {
    audio_destroy(audio);
  }

  emu->running = 0;

  return 0;
}

void emu_run(struct emu *emu, const char *path) {
  /* load gdi / bin if specified */
  if (path) {
    int launched = 0;

    LOG_INFO("Launching %s", path);

    if ((strstr(path, ".bin") && emu_launch_bin(emu, path)) ||
        (strstr(path, ".gdi") && emu_launch_gdi(emu, path))) {
      launched = 1;
    }

    if (!launched) {
      LOG_WARNING("Failed to launch %s", path);
      return;
    }
  }
  /* else, boot to main menu */
  else {
    sh4_reset(emu->dc->sh4, 0xa0000000);
    dc_resume(emu->dc);
  }

  emu->running = 1;

  /* emulator, audio and video all run on their own threads. the high-level
     design is that the emulator behaves much like a codec, in that it
     produces complete frames of decoded data, and the audio and video
     thread are responsible for simply presenting the data */
  thread_t core_thread = thread_create(&emu_core_thread, NULL, emu);

  while (emu->running) {
    win_pump_events(emu->window);
    emu_paint(emu);
  }

  /* wait for the core thread to exit */
  void *result;
  thread_join(core_thread, &result);
}

void emu_destroy(struct emu *emu) {
  tr_destroy(emu->tr);
  nk_destroy(emu->nk);
  mp_destroy(emu->mp);
  rb_destroy(emu->rb);
  dc_destroy(emu->dc);

  win_remove_listener(emu->window, &emu->listener);

  free(emu);
}

struct emu *emu_create(struct window *window) {
  struct emu *emu = calloc(1, sizeof(struct emu));

  emu->window = window;

  /* add window input listeners */
  emu->listener = (struct window_listener){
      emu, &emu_joy_add, &emu_joy_remove, &emu_keydown, NULL, &emu_close, {0}};
  win_add_listener(emu->window, &emu->listener);

  /* setup dreamcast */
  emu->dc = dc_create();

  /* setup render backend */
  emu->rb = rb_create(emu->window);
  emu->mp = mp_create(emu->window, emu->rb);
  emu->nk = nk_create(emu->window, emu->rb);
  emu->tr = tr_create(emu->rb, ta_texture_provider(emu->dc->ta));

  /* debug menu enabled by default */
  emu->debug_menu = 1;

  return emu;
}
