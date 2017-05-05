#include "emulator.h"
#include "core/option.h"
#include "core/profiler.h"
#include "host.h"
#include "hw/aica/aica.h"
#include "hw/arm7/arm7.h"
#include "hw/dreamcast.h"
#include "hw/gdrom/gdrom.h"
#include "hw/maple/maple.h"
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
#include "video/render_backend.h"

DEFINE_AGGREGATE_COUNTER(frames);

#define MAX_VIDEO_FRAMES 4

struct video_frame {
  /* framebuffer handle */
  framebuffer_handle_t fb;

  /* texture handle for the framebuffer's color component */
  texture_handle_t fb_tex;

  /* fence to ensure framebuffer has finished rendering before presenting */
  sync_handle_t fb_sync;

  struct list_node it;
};

struct emu {
  int multi_threaded;
  struct host *host;
  struct dreamcast *dc;
  volatile int running;
  volatile int rendered;
  int debug_menu;
  int initialized;

  /* render state */
  struct render_backend *r, *r2;
  struct audio_backend *audio;
  struct microprofile *mp;
  struct nuklear *nk;
  struct tr *tr;
  struct tile_render_context rc;

  /* pool of offscreen framebuffers */
  mutex_t frames_mutex;
  struct video_frame frames[MAX_VIDEO_FRAMES];
  struct list free_frames;
  struct list live_frames;

  /* video thread state */
  thread_t video_thread;
  mutex_t pending_mutex;
  cond_t pending_cond;
  struct tile_ctx *pending_ctx;
};

static struct video_frame *emu_pop_frame(struct emu *emu) {
  mutex_lock(emu->frames_mutex);

  /* return the newest frame that's ready to be presented */
  struct video_frame *frame =
      list_first_entry(&emu->live_frames, struct video_frame, it);

  if (frame) {
    list_remove_entry(&emu->live_frames, frame, it);
  }

  mutex_unlock(emu->frames_mutex);

  return frame;
}

static void emu_push_front_frame(struct emu *emu, struct video_frame *frame) {
  /* called from the video thread when it's done rendering a frame. at this
     point, free any frames that were previously queued for presentation */
  mutex_lock(emu->frames_mutex);

  while (!list_empty(&emu->live_frames)) {
    struct video_frame *head =
        list_first_entry(&emu->live_frames, struct video_frame, it);
    list_remove(&emu->live_frames, &head->it);
    list_add(&emu->free_frames, &head->it);
  }

  list_add_after(&emu->live_frames, NULL, &frame->it);

  mutex_unlock(emu->frames_mutex);
}

static void emu_push_back_frame(struct emu *emu, struct video_frame *frame) {
  /* called from the main thread when it's done presenting a frame */
  mutex_lock(emu->frames_mutex);

  list_add(&emu->live_frames, &frame->it);

  mutex_unlock(emu->frames_mutex);
}

static struct video_frame *emu_alloc_frame(struct emu *emu,
                                           struct render_backend *r) {
  /* return the first free frame to be rendered to. note, the free list should
     only be modified by the video thread, so there's no need to lock */
  struct video_frame *frame =
      list_first_entry(&emu->free_frames, struct video_frame, it);
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

static void emu_render_frame(struct emu *emu) {
  struct render_backend *fb_rb = emu->multi_threaded ? emu->r2 : emu->r;

  prof_counter_add(COUNTER_frames, 1);

  /* render the current render context to the first free framebuffer */
  struct video_frame *frame = emu_alloc_frame(emu, fb_rb);
  framebuffer_handle_t original = r_get_framebuffer(fb_rb);
  r_bind_framebuffer(fb_rb, frame->fb);
  r_clear_viewport(fb_rb);
  tr_render_context(emu->tr, &emu->rc);
  r_bind_framebuffer(fb_rb, original);

  /* insert fence for main thread to synchronize on in order to ensure that
     the context has completely rendered */
  if (emu->multi_threaded) {
    frame->fb_sync = r_insert_sync(fb_rb);
  }

  /* push frame to the presentation queue for the main thread */
  emu_push_front_frame(emu, frame);

  /* update frame-based profiler stats */
  prof_flip();
}

static void *emu_video_thread(void *data) {
  struct emu *emu = data;

  /* make secondary context active for this thread */
  r_make_current(emu->r2);

  while (emu->running) {
    mutex_lock(emu->pending_mutex);

    /* wait for the next tile context provided by emu_start_render */
    cond_wait(emu->pending_cond, emu->pending_mutex);

    /* shutting down */
    if (!emu->pending_ctx) {
      continue;
    }

    /* parse the context, uploading its textures to the render backend */
    tr_parse_context(emu->tr, emu->pending_ctx, &emu->rc);

    /* after the context has been parsed, release the mutex to let
       emu_finish_render complete */
    mutex_unlock(emu->pending_mutex);

    /* note, during this window of time (after releasing the mutex, and before
       emu_render_frame ends), a frame could be processed by emu_start_render /
       emu_finish_rendered that is skipped */

    /* render the parsed context to an offscreen framebuffer */
    emu_render_frame(emu);
  }

  return NULL;
}

static void emu_paint(struct emu *emu) {
  float w = (float)video_width(emu->host);
  float h = (float)video_height(emu->host);

  nk_update_input(emu->nk);

  r_clear_viewport(emu->r);

  /* present the latest frame from the video thread */
  struct video_frame *frame = emu_pop_frame(emu);

  if (frame) {
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
    struct nk_rect bounds = {0.0f, -1.0f, w, DEBUG_MENU_HEIGHT};

    nk_style_default(ctx);

    ctx->style.window.border = 0.0f;
    ctx->style.window.menu_border = 0.0f;
    ctx->style.window.spacing = nk_vec2(0.0f, 0.0f);
    ctx->style.window.padding = nk_vec2(0.0f, 0.0f);

    if (nk_begin(ctx, "debug menu", bounds, NK_WINDOW_NO_SCROLLBAR)) {
      static int max_debug_menus = 32;

      nk_menubar_begin(ctx);
      nk_layout_row_begin(ctx, NK_STATIC, DEBUG_MENU_HEIGHT, max_debug_menus);

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

      nk_layout_row_push(ctx, w - ctx->current->layout->row.item_offset);
      nk_label(ctx, status, NK_TEXT_RIGHT);

      nk_layout_row_end(ctx);
      nk_menubar_end(ctx);
    }
    nk_end(ctx);
  }

  mp_render(emu->mp);
  nk_render(emu->nk);

  if (frame) {
    /* after buffers have been swapped, the frame has been completely
       rendered and can safely be reused */
    emu_push_back_frame(emu, frame);
  }
}

static void emu_poll_input(void *userdata) {
  struct emu *emu = userdata;

  input_poll(emu->host);
}

static void emu_finish_render(void *userdata) {
  struct emu *emu = userdata;

  if (emu->multi_threaded) {
    /* ideally, the video thread has parsed the pending context, uploaded its
       textures, etc. during the estimated render time. however, if it hasn't
       finished, the emulation thread must be paused to avoid altering
       the yet-to-be-uploaded texture memory */
    mutex_lock(emu->pending_mutex);

    emu->pending_ctx = NULL;

    mutex_unlock(emu->pending_mutex);
  }

  /* let the main thread know a frame has been rendered */
  emu->rendered = 1;
}

static void emu_start_render(void *userdata, struct tile_ctx *ctx) {
  struct emu *emu = userdata;

  if (emu->multi_threaded) {
    /* save off context and notify video thread that it's available */
    mutex_lock(emu->pending_mutex);

    emu->pending_ctx = ctx;
    cond_signal(emu->pending_cond);

    mutex_unlock(emu->pending_mutex);
  } else {
    /* parse the context and immediately render it */
    tr_parse_context(emu->tr, ctx, &emu->rc);

    emu_render_frame(emu);
  }
}

static void emu_push_audio(void *userdata, const int16_t *data, int frames) {
  struct emu *emu = userdata;
  audio_push(emu->host, data, frames);
}

static void emu_input_controller(void *userdata, int port, int button,
                                 int16_t value) {
  struct emu *emu = userdata;

  dc_input(emu->dc, port, button, value);
}

static void emu_input_mouse(void *userdata, int x, int y) {
  struct emu *emu = userdata;

  mp_mousemove(emu->mp, x, y);
  nk_mousemove(emu->nk, x, y);
}

static void emu_input_keyboard(void *userdata, enum keycode key,
                               int16_t value) {
  struct emu *emu = userdata;

  if (key == K_F1 && value > 0) {
    emu->debug_menu = emu->debug_menu ? 0 : 1;
  } else {
    mp_keydown(emu->mp, key, value);
    nk_keydown(emu->nk, key, value);
  }
}

static void emu_video_context_destroyed(void *userdata) {
  struct emu *emu = userdata;
  struct texture_provider *provider = ta_texture_provider(emu->dc->ta);

  if (!emu->initialized) {
    return;
  }

  /* destroy the video thread */
  if (emu->multi_threaded) {
    /* signal to break out of its loop */
    mutex_lock(emu->pending_mutex);
    emu->pending_ctx = NULL;
    cond_signal(emu->pending_cond);
    mutex_unlock(emu->pending_mutex);

    /* wait for it to exit */
    void *result;
    thread_join(emu->video_thread, &result);
  }

  /* reset texture cache
     TODO this feels kind of wrong, should we be managing the texture cache? */
  provider->clear_textures(provider->userdata);

  /* destroy offscreen framebuffer pool */
  struct render_backend *fb_rb = emu->multi_threaded ? emu->r2 : emu->r;

  r_make_current(fb_rb);

  for (int i = 0; i < MAX_VIDEO_FRAMES; i++) {
    struct video_frame *frame = &emu->frames[i];

    r_destroy_framebuffer(fb_rb, frame->fb);

    if (frame->fb_sync) {
      r_destroy_sync(fb_rb, frame->fb_sync);
    }
  }

  mutex_destroy(emu->frames_mutex);

  tr_destroy(emu->tr);

  if (emu->multi_threaded) {
    r_destroy(emu->r2);
    cond_destroy(emu->pending_cond);
    mutex_destroy(emu->pending_mutex);
  }

  nk_destroy(emu->nk);
  mp_destroy(emu->mp);
  r_destroy(emu->r);
}

static void emu_video_context_reset(void *userdata) {
  struct emu *emu = userdata;
  struct texture_provider *provider = ta_texture_provider(emu->dc->ta);

  emu_video_context_destroyed(userdata);

  emu->r = r_create(emu->host);
  emu->mp = mp_create(emu->r);
  emu->nk = nk_create(emu->r);

  if (emu->multi_threaded) {
    emu->pending_mutex = mutex_create();
    emu->pending_cond = cond_create();
    emu->r2 = r_create_from(emu->r);
  }

  /* create pool of offscreen framebuffers */
  struct render_backend *fb_rb = emu->multi_threaded ? emu->r2 : emu->r;
  emu->tr = tr_create(fb_rb, provider);

  r_make_current(fb_rb);

  emu->frames_mutex = mutex_create();

  for (int i = 0; i < MAX_VIDEO_FRAMES; i++) {
    struct video_frame *frame = &emu->frames[i];
    frame->fb = r_create_framebuffer(fb_rb, &frame->fb_tex);
    list_add(&emu->free_frames, &frame->it);
  }

  /* make primary renderer active for the current thread */
  r_make_current(emu->r);

  /* startup video thread */
  if (emu->multi_threaded) {
    emu->video_thread = thread_create(&emu_video_thread, NULL, emu);
    CHECK_NOTNULL(emu->video_thread);
  }
}

void emu_run(struct emu *emu) {
  static const int64_t MACHINE_STEP = HZ_TO_NANO(1000);

  emu->rendered = 0;

  while (!emu->rendered) {
    dc_tick(emu->dc, MACHINE_STEP);
  }

  prof_update(time_nanoseconds());

  emu_paint(emu);
}

int emu_load_game(struct emu *emu, const char *path) {
  if (!dc_load(emu->dc, path)) {
    return 0;
  }

  dc_resume(emu->dc);

  return 1;
}

void emu_destroy(struct emu *emu) {
  emu->running = 0;

  /* destroy dreamcast */
  dc_destroy(emu->dc);

  free(emu);
}

struct emu *emu_create(struct host *host) {
  struct emu *emu = calloc(1, sizeof(struct emu));

  emu->running = 1;

  /* setup host, bind event callbacks */
  emu->host = host;
  emu->host->userdata = emu;
  emu->host->video_context_reset = &emu_video_context_reset;
  emu->host->video_context_destroyed = &emu_video_context_destroyed;
  emu->host->input_keyboard = &emu_input_keyboard;
  emu->host->input_mouse = &emu_input_mouse;
  emu->host->input_controller = &emu_input_controller;

  /* create dreamcast, bind client callbacks */
  emu->dc = dc_create();
  emu->dc->userdata = emu;
  emu->dc->push_audio = &emu_push_audio;
  emu->dc->start_render = &emu_start_render;
  emu->dc->finish_render = &emu_finish_render;
  emu->dc->poll_input = &emu_poll_input;

  /* start up the video thread */
  emu->multi_threaded = video_gl_supports_multiple_contexts(emu->host);

  /* enable debug menu by default */
  emu->debug_menu = 1;

  return emu;
}
