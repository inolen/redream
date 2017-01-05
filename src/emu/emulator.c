#include "emu/emulator.h"
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
#include "ui/nuklear.h"
#include "ui/window.h"

DEFINE_AGGREGATE_COUNTER(frames);

struct emu {
  struct window *window;
  struct window_listener listener;
  struct dreamcast *dc;
  int running;

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

static void emu_paint(void *data) {
  struct emu *emu = data;

  /* wait for the next ta context */
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

  prof_counter_add(COUNTER_frames, 1);

  prof_flip();
}

static void emu_debug_menu(void *data, struct nk_context *ctx) {
  struct emu *emu = data;

  /* set status string */
  char status[128];

  int frames = (int)prof_counter_load(COUNTER_frames);
  int ta_renders = (int)prof_counter_load(COUNTER_ta_renders);
  int pvr_vblanks = (int)prof_counter_load(COUNTER_pvr_vblanks);
  int sh4_instrs = (int)(prof_counter_load(COUNTER_sh4_instrs) / 1000000.0f);
  int arm7_instrs = (int)(prof_counter_load(COUNTER_arm7_instrs) / 1000000.0f);

  snprintf(status, sizeof(status), "%3d FPS %3d RPS %3d VBS %4d SH4 %d ARM",
           frames, ta_renders, pvr_vblanks, sh4_instrs, arm7_instrs);
  win_set_status(emu->window, status);

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
  struct audio_backend *audio = audio_create(emu->dc->aica);

  if (!audio) {
    LOG_WARNING("Audio backend creation failed");
    return 0;
  }

  /* main emulation loop

     unlike the real machine which runs multiple hardware devices in parallel,
     all of the emulated hardware in redream is ran synchronously, in a
     cooperative multitasking fashion. this removes numerous complexities in
     the c code, as well as the runtime generated code.

     on creation, each hardware device registers itself with the scheduler
     interface. this scheduler interface is used by dc_tick to run each device
     for the specified slice of guest time. baring in mind that each device is
     ran synchronously, this slice should be low enough that devices waiting on
     interrupts from eachother are serviced regularly, but high enough that
     there's not too much context switching. please note, it's extremely
     important that this slice is constant to keep emulation deterministic
     between runs.

     the next issue tackled by this loop is, when should dc_tick be called to
     execute this constant slice of time. the answer really depends on what
     the goal of emulation is.

     when the goal is to run completely unthrottled, it should be called as much
     as possible, e.g.:

       while (1) {
         dc_tick(slice);
       }

     when the goal is to run at the same speed as the original dreamcast, the
     answer is a bit more involved. at first it may seem desirable to use the
     host machine's clock to schedule each slice, e.g.:

       while (1) {
         current_time = time();
         delta_time = next_time - current_time;

         if (delta_time < 0) {
           dc_tick(slice);
           next_time = current_time + delta_time + slice;
         }
       }

     this will, in general, run the emulator at the same rate as the original
     dreamcast. when performance hiccups, the host's time domain will move
     forward, while the emulator's time domain will fall behind. the emulator
     will then speed up temporarily due to the delta_time offset, eventually
     synchronizing it's view of time with the host as delta_time approaches 0.

     the downsides to this approach are audio, and video to some degree, are
     not presented well when performance hiccups. imagine the scenario that
     performance grinds to a complete halt for 5 seconds. in this case, host
     time is 5 seconds ahead of guest time, the loop will run 5 seconds worth
     of emulator time in say, 1 second of host time, again synchronizing the
     time domains. the problem being that, now 5 seconds of audio and video
     have been generated for something the user has experienced for only 1
     second. skipping video frames in this case isn't the worst experience
     but crackling and distorted audio can be awful. */

  static const int64_t MACHINE_STEP = HZ_TO_NANO(1000);
  int64_t current_time = 0;
  int64_t next_pump_time = 0;

  while (emu->running) {
    while (audio_buffer_low(audio)) {
      dc_tick(emu->dc, MACHINE_STEP);
    }

    /* audio events are just for device connections, check infrequently */
    current_time = time_nanoseconds();

    if (current_time > next_pump_time) {
      audio_pump_events(audio);
      next_pump_time = current_time + NS_PER_SEC;
    }

    prof_update(current_time);
  }

  audio_destroy(audio);

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
  }

  /* wait for the core thread to exit */
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
  emu->listener = (struct window_listener){emu,
                                           &emu_paint,
                                           &emu_debug_menu,
                                           &emu_joy_add,
                                           &emu_joy_remove,
                                           &emu_keydown,
                                           NULL,
                                           NULL,
                                           &emu_close,
                                           {0}};
  win_add_listener(emu->window, &emu->listener);

  /* enable debug menu by default */
  win_enable_debug_menu(emu->window, 1);

  return emu;
}
