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

#define MAX_FRAMES 8

struct frame {
  /* framebuffer handle */
  framebuffer_handle_t fb;
  /* texture handle for the framebuffer's color component */
  texture_handle_t fb_tex;
  /* fence to ensure framebuffer has finished rendering before presenting */
  sync_handle_t fb_sync;

  struct list_node it;
};

struct emu {
  struct window *win;
  struct window_listener listener;
  struct dreamcast *dc;

  struct render_backend *r;
  struct audio_backend *audio;
  struct microprofile *mp;
  struct nuklear *nk;

  volatile int running;
  int debug_menu;

  /* pool of offscreen framebuffers used for rendering the video display */
  mutex_t frames_mutex;
  struct frame frames[MAX_FRAMES];
  struct list free_frames;
  struct list live_frames;
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

static struct frame *emu_pop_frame(struct emu *emu) {
  mutex_lock(emu->frames_mutex);

  /* return the newest frame that's ready to be presented */
  struct frame *frame = list_first_entry(&emu->live_frames, struct frame, it);

  if (frame) {
    list_remove_entry(&emu->live_frames, frame, it);
  }

  mutex_unlock(emu->frames_mutex);

  return frame;
}

static void emu_push_front_frame(struct emu *emu, struct frame *frame) {
  /* called from the video thread when it's done rendering a frame. at this
     point, free any frames that were previously queued for presentation */
  mutex_lock(emu->frames_mutex);

  while (!list_empty(&emu->live_frames)) {
    struct frame *head = list_first_entry(&emu->live_frames, struct frame, it);
    list_remove(&emu->live_frames, &head->it);
    list_add(&emu->free_frames, &head->it);
  }

  list_add_after(&emu->live_frames, NULL, &frame->it);

  mutex_unlock(emu->frames_mutex);
}

static void emu_push_back_frame(struct emu *emu, struct frame *frame) {
  /* called from the main thread when it's done presenting a frame */
  mutex_lock(emu->frames_mutex);

  list_add(&emu->live_frames, &frame->it);

  mutex_unlock(emu->frames_mutex);
}

static struct frame *emu_alloc_frame(struct emu *emu,
                                     struct render_backend *r) {
  /* return the first free frame to be rendered to. note, the free list should
     only be modified by the video thread, so there's no need to lock */
  struct frame *frame = list_first_entry(&emu->free_frames, struct frame, it);
  CHECK_NOTNULL(frame);
  list_remove_entry(&emu->free_frames, frame, it);

  /* reset frame state */
  CHECK_NOTNULL(frame->fb);
  CHECK_NOTNULL(frame->fb_tex);

  if (frame->fb_sync) {
    r_destroy_sync(r, frame->fb_sync);
    frame->fb_sync = 0;
  }

  return frame;
}

static void emu_destroy_frames(struct emu *emu, struct render_backend *r) {
  for (int i = 0; i < MAX_FRAMES; i++) {
    struct frame *frame = &emu->frames[i];

    r_destroy_framebuffer(r, frame->fb);

    if (frame->fb_sync) {
      r_destroy_sync(r, frame->fb_sync);
    }
  }
}

static void emu_create_frames(struct emu *emu, struct render_backend *r) {
  for (int i = 0; i < MAX_FRAMES; i++) {
    struct frame *frame = &emu->frames[i];

    frame->fb = r_create_framebuffer(r, &frame->fb_tex);

    list_add(&emu->free_frames, &frame->it);
  }
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

static void emu_paint(struct emu *emu) {
  prof_counter_add(COUNTER_frames, 1);

  r_clear_viewport(emu->r);

  nk_update_input(emu->nk);

  /* present the latest frame from the video thread */
  struct frame *frame = emu_pop_frame(emu);

  if (frame) {
    float w = (float)emu->win->width;
    float h = (float)emu->win->height;

    struct vertex2 verts[6] = {
        /* triangle 1, top left  */
        {{0.0f, 0.0f}, {0.0f, 1.0f}, 0xffffffff},
        /* triangle 1, top right */
        {{w, 0.0f}, {1.0f, 1.0f}, 0xffffffff},
        /* triangle 1, bottom left */
        {{0.0f, h}, {0.0f, 0.0f}, 0xffffffff},
        /* triangle 2, top right */
        {{w, 0.0f}, {1.0f, 1.0f}, 0xffffffff},
        /* triangle 2, bottom right */
        {{w, h}, {1.0f, 0.0f}, 0xffffffff},
        /* triangle 2, bottom left */
        {{0.0f, h}, {0.0f, 0.0f}, 0xffffffff},
    };

    struct surface2 quad = {0};
    quad.prim_type = PRIM_TRIANGLES;
    quad.texture = frame->fb_tex;
    quad.src_blend = BLEND_NONE;
    quad.dst_blend = BLEND_NONE;
    quad.first_vert = 0;
    quad.num_verts = 6;

    /* wait for the frame to finish rendering */
    if (frame->fb_sync) {
      r_wait_sync(emu->r, frame->fb_sync);
      r_destroy_sync(emu->r, frame->fb_sync);
      frame->fb_sync = 0;
    }

    r_begin_ortho(emu->r);
    r_begin_surfaces2(emu->r, verts, 6, NULL, 0);
    r_draw_surface2(emu->r, &quad);
    r_end_surfaces2(emu->r);
    r_end_ortho(emu->r);
  }

  /* render debug menus */
  if (emu->debug_menu) {
    struct nk_context *ctx = &emu->nk->ctx;
    struct nk_rect bounds = {0.0f, 0.0f, (float)emu->win->width,
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

        int fullscreen = emu->win->fullscreen;
        if (nk_checkbox_label(ctx, "fullscreen", &fullscreen)) {
          win_set_fullscreen(emu->win, fullscreen);
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

      snprintf(status, sizeof(status), "FPS %3d RPS %3d VBS %3d SH4 %4d ARM %d",
               frames, ta_renders, pvr_vblanks, sh4_instrs, arm7_instrs);

      nk_layout_row_push(
          ctx, (float)emu->win->width - ctx->current->layout->row.item_offset);
      nk_label(ctx, status, NK_TEXT_RIGHT);

      nk_layout_row_end(ctx);
      nk_menubar_end(ctx);
    }
    nk_end(ctx);
  }

  mp_render(emu->mp);
  nk_render(emu->nk);

  r_swap_buffers(emu->r);

  /* after buffers have been swapped, the frame has been completely
     rendered and can safely be reused */
  if (frame) {
    emu_push_back_frame(emu, frame);
  }
}

static void *emu_video_thread(void *data) {
  struct emu *emu = data;

  /* create additional renderer on this thread for rendering the tile contexts
     to offscreen framebuffers */
  struct render_backend *r = r_create_from(emu->r);
  struct tr *tr = tr_create(r, ta_texture_provider(emu->dc->ta));

  struct tile_ctx *pending_ctx;
  struct tile_render_context *rc = malloc(sizeof(struct tile_render_context));

  emu_create_frames(emu, r);

  while (emu->running) {
    /* wait for the main thread to publish the next ta context to be rendered */
    if (!ta_lock_pending_context(emu->dc->ta, &pending_ctx, 1000)) {
      continue;
    }

    /* parse the context, uploading textures it uses to the render backend */
    tr_parse_context(tr, pending_ctx, rc);

    /* after uploading the textures, unlock to let the main thread resume */
    ta_unlock_pending_context(emu->dc->ta);

    /* render the context to the first free framebuffer */
    struct frame *frame = emu_alloc_frame(emu, r);
    r_bind_framebuffer(r, frame->fb);
    r_clear_viewport(r);
    tr_render_context(tr, rc);

    /* insert fence for main thread to synchronize on in order to ensure that
       the context has completely rendered */
    frame->fb_sync = r_insert_sync(r);

    /* push frame to the presentation queue for the main thread */
    emu_push_front_frame(emu, frame);

    /* update frame-based profiler stats */
    prof_flip();
  }

  emu_destroy_frames(emu, r);

  free(rc);

  tr_destroy(tr);
  r_destroy(r);

  return NULL;
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

  /* emulator, audio and video all run on their own threads. the high-level
     design is that the emulator behaves much like a codec, in that it
     produces complete frames of decoded data, and the audio and video
     threads are responsible for presenting the data */
  static const int64_t MACHINE_STEP = HZ_TO_NANO(1000);
  static const int64_t EVENT_STEP = HZ_TO_NANO(60);
  int64_t current_time = 0;
  int64_t next_pump_time = 0;

  emu->running = 1;

  thread_t video_thread = thread_create(&emu_video_thread, NULL, emu);

  while (emu->running) {
    /* run a slice of dreamcast time if the available audio is running low. this
       effectively synchronizes the emulation speed with the host audio clock.
       note however, if audio is disabled, the emulator will run as fast as
       possible */
    if (!emu->audio || audio_buffer_low(emu->audio)) {
      dc_tick(emu->dc, MACHINE_STEP);
    }

    /* FIXME this needs to be refactored:
       - profile stats do need to be updated in a similar fashion. however,
         it'd be much more valuable to update them based on the guest time,
         not host time. the profiler should probably schedule a recurring
         event through the scheduler interface
       - audio events code needs to be moved to a dedicated audio thread
         and out of here
       - win_pump_events should be scheduled based on guest time using the
         scheduler interface such that controller input is provided at a
         deterministic rate
       - vsync should be enabled, and emu_paint only called if there is a new
         frame to render
    */
    current_time = time_nanoseconds();

    if (current_time > next_pump_time) {
      prof_update(current_time);

      if (emu->audio) {
        audio_pump_events(emu->audio);
      }

      win_pump_events(emu->win);

      emu_paint(emu);

      next_pump_time = current_time + EVENT_STEP;
    }
  }

  /* wait for video thread to exit */
  void *result;
  thread_join(video_thread, &result);
}

void emu_destroy(struct emu *emu) {
  /* destroy audio backend */
  {
    if (emu->audio) {
      audio_destroy(emu->audio);
    }
  }

  /* destroy render backend */
  {
    mutex_destroy(emu->frames_mutex);
    nk_destroy(emu->nk);
    mp_destroy(emu->mp);
    r_destroy(emu->r);
  }

  /* destroy dreamcast */
  dc_destroy(emu->dc);

  win_remove_listener(emu->win, &emu->listener);

  free(emu);
}

struct emu *emu_create(struct window *win) {
  struct emu *emu = calloc(1, sizeof(struct emu));

  emu->win = win;

  /* add window input listeners */
  emu->listener = (struct window_listener){
      emu, &emu_joy_add, &emu_joy_remove, &emu_keydown, NULL, &emu_close, {0}};
  win_add_listener(emu->win, &emu->listener);

  /* create dreamcast */
  emu->dc = dc_create();

  /* create render backend */
  {
    emu->r = r_create(emu->win);
    emu->mp = mp_create(emu->win, emu->r);
    emu->nk = nk_create(emu->win, emu->r);
    emu->frames_mutex = mutex_create();
  }

  /* create audio backend */
  {
    if (OPTION_audio) {
      emu->audio = audio_create(emu->dc->aica);
    }
  }

  /* debug menu enabled by default */
  emu->debug_menu = 1;

  return emu;
}
