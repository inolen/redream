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
#include "render/microprofile.h"
#include "render/nuklear.h"
#include "render/render_backend.h"
#include "sys/thread.h"
#include "sys/time.h"

DEFINE_AGGREGATE_COUNTER(frames);

enum {
  FRAME_WAITING,
  FRAME_RENDERING,
  FRAME_RENDERED,
};

struct emu {
  int multi_threaded;
  struct host *host;
  struct dreamcast *dc;
  volatile int running;
  int debug_menu;

  struct render_backend *r, *r2;
  struct microprofile *mp;
  struct nuklear *nk;
  struct tr *tr;

  /* video state */
  thread_t video_thread;

  mutex_t pending_mutex;
  cond_t pending_cond;
  struct tile_ctx *pending_ctx;

  volatile int video_state;
  volatile int video_width;
  volatile int video_height;

  struct tile_render_context video_ctx;
  int video_fb_width;
  int video_fb_height;
  framebuffer_handle_t video_fb;
  texture_handle_t video_tex;
  sync_handle_t video_sync;
};

static struct render_backend *emu_video_renderer(struct emu *emu) {
  /* when using multi-threaded rendering, the video thread has its own render
     backend instance */
  return emu->multi_threaded ? emu->r2 : emu->r;
}

static void emu_render_frame(struct emu *emu) {
  struct render_backend *r2 = emu_video_renderer(emu);

  prof_counter_add(COUNTER_frames, 1);

  emu->video_state = FRAME_RENDERING;

  /* resize the framebuffer at this time if the output size has changed */
  if (emu->video_fb_width != emu->video_width ||
      emu->video_fb_height != emu->video_height) {
    r_destroy_framebuffer(r2, emu->video_fb);

    emu->video_fb_width = emu->video_width;
    emu->video_fb_height = emu->video_height;
    emu->video_fb = r_create_framebuffer(r2, emu->video_fb_width,
                                         emu->video_fb_height, &emu->video_tex);
  }

  /* render the current render context to the video framebuffer */
  framebuffer_handle_t original = r_get_framebuffer(r2);
  r_bind_framebuffer(r2, emu->video_fb);
  tr_render_context(emu->tr, &emu->video_ctx, emu->video_fb_width,
                    emu->video_fb_height);
  r_bind_framebuffer(r2, original);

  /* insert fence for main thread to synchronize on in order to ensure that
     the context has completely rendered */
  if (emu->multi_threaded) {
    emu->video_sync = r_insert_sync(r2);
  }

  /* update frame-based profiler stats */
  prof_flip();

  emu->video_state = FRAME_RENDERED;
}

static void *emu_video_thread(void *data) {
  struct emu *emu = data;

  /* make secondary context active for this thread */
  r_make_current(emu->r2);

  while (1) {
    mutex_lock(emu->pending_mutex);

    /* wait for the next tile context provided by emu_start_render */
    while (!emu->pending_ctx) {
      cond_wait(emu->pending_cond, emu->pending_mutex);
    }

    /* check for shutdown */
    if (!emu->running) {
      mutex_unlock(emu->pending_mutex);
      break;
    }

    /* parse the context, uploading its textures to the render backend */
    tr_parse_context(emu->tr, emu->pending_ctx, &emu->video_ctx);
    emu->pending_ctx = NULL;

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
  int width = emu->video_width;
  int height = emu->video_height;
  float fwidth = (float)emu->video_width;
  float fheight = (float)emu->video_height;

  nk_update_input(emu->nk);

  r_clear_viewport(emu->r, width, height);

  /* present the latest frame from the video thread */
  {
    struct vertex2 verts[6] = {
        /* triangle 1, top left  */
        {{0.0f, 0.0f}, {0.0f, 1.0f}, 0xffffffff},
        /* triangle 1, top right */
        {{fwidth, 0.0f}, {1.0f, 1.0f}, 0xffffffff},
        /* triangle 1, bottom left */
        {{0.0f, fheight}, {0.0f, 0.0f}, 0xffffffff},
        /* triangle 2, top right */
        {{fwidth, 0.0f}, {1.0f, 1.0f}, 0xffffffff},
        /* triangle 2, bottom right */
        {{fwidth, fheight}, {1.0f, 0.0f}, 0xffffffff},
        /* triangle 2, bottom left */
        {{0.0f, fheight}, {0.0f, 0.0f}, 0xffffffff},
    };

    struct surface2 quad = {0};
    quad.prim_type = PRIM_TRIANGLES;
    quad.texture = emu->video_tex;
    quad.src_blend = BLEND_NONE;
    quad.dst_blend = BLEND_NONE;
    quad.first_vert = 0;
    quad.num_verts = 6;

    /* wait for the frame to finish rendering */
    if (emu->video_sync) {
      r_wait_sync(emu->r, emu->video_sync);
      r_destroy_sync(emu->r, emu->video_sync);
      emu->video_sync = 0;
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
    struct nk_rect bounds = {0.0f, -1.0f, fwidth, DEBUG_MENU_HEIGHT + 1.0f};

    nk_style_default(ctx);
    ctx->style.window.border = 0.0f;
    ctx->style.window.menu_border = 0.0f;
    ctx->style.window.spacing = nk_vec2(0.0f, 0.0f);
    ctx->style.window.padding = nk_vec2(0.0f, 0.0f);

    if (nk_begin(ctx, "debug menu", bounds, NK_WINDOW_NO_SCROLLBAR)) {
      static int max_debug_menus = 32;

      nk_style_default(ctx);
      ctx->style.window.padding = nk_vec2(0.0f, 0.0f);

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

      nk_layout_row_push(ctx, fwidth - ctx->current->layout->row.item_offset);
      nk_label(ctx, status, NK_TEXT_RIGHT);

      nk_layout_row_end(ctx);
      nk_menubar_end(ctx);
    }
    nk_end(ctx);
  }

  mp_render(emu->mp);
  nk_render(emu->nk);
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
}

static void emu_start_render(void *userdata, struct tile_ctx *ctx) {
  struct emu *emu = userdata;

  if (emu->video_state != FRAME_WAITING) {
    /* skip this frame if one is already rendering / rendered */
    return;
  }

  if (emu->multi_threaded) {
    /* save off context and notify video thread that it's available */
    mutex_lock(emu->pending_mutex);

    emu->pending_ctx = ctx;
    cond_signal(emu->pending_cond);

    mutex_unlock(emu->pending_mutex);
  } else {
    /* parse the context and immediately render it */
    tr_parse_context(emu->tr, ctx, &emu->video_ctx);

    emu_render_frame(emu);
  }
}

static void emu_push_audio(void *userdata, const int16_t *data, int frames) {
  struct emu *emu = userdata;
  audio_push(emu->host, data, frames);
}

static void emu_input_mousemove(void *userdata, int port, int x, int y) {
  struct emu *emu = userdata;

  mp_mousemove(emu->mp, x, y);
  nk_mousemove(emu->nk, x, y);
}

static void emu_input_keydown(void *userdata, int port, enum keycode key,
                              int16_t value) {
  struct emu *emu = userdata;

  if (key == K_F1 && value > 0) {
    emu->debug_menu = emu->debug_menu ? 0 : 1;
  } else {
    mp_keydown(emu->mp, key, value);
    nk_keydown(emu->nk, key, value);
  }

  if (key >= K_CONT_C && key <= K_CONT_RTRIG) {
    dc_input(emu->dc, port, key - K_CONT_C, value);
  }
}

static void emu_video_context_destroyed(void *userdata) {
  struct emu *emu = userdata;
  struct texture_provider *provider = ta_texture_provider(emu->dc->ta);

  if (!emu->running) {
    return;
  }

  emu->running = 0;

  /* destroy the video thread */
  if (emu->multi_threaded) {
    mutex_lock(emu->pending_mutex);
    emu->pending_ctx = (struct tile_ctx *)0xdeadbeef;
    cond_signal(emu->pending_cond);
    mutex_unlock(emu->pending_mutex);

    void *result;
    thread_join(emu->video_thread, &result);
  }

  /* reset texture cache
     TODO this feels kind of wrong, should we be managing the texture cache? */
  provider->clear_textures(provider->userdata);

  /* destroy video renderer state */
  struct render_backend *r2 = emu_video_renderer(emu);

  r_make_current(r2);

  r_destroy_framebuffer(r2, emu->video_fb);
  if (emu->video_sync) {
    r_destroy_sync(r2, emu->video_sync);
  }
  tr_destroy(emu->tr);

  if (emu->multi_threaded) {
    r_destroy(emu->r2);
    cond_destroy(emu->pending_cond);
    mutex_destroy(emu->pending_mutex);
  }

  /* destroy primary renderer state */
  r_make_current(emu->r);

  nk_destroy(emu->nk);
  mp_destroy(emu->mp);
  r_destroy(emu->r);
}

static void emu_video_context_reset(void *userdata) {
  struct emu *emu = userdata;
  struct texture_provider *provider = ta_texture_provider(emu->dc->ta);

  emu_video_context_destroyed(userdata);

  emu->running = 1;

  /* create primary renderer */
  emu->r = r_create(emu->host);
  emu->mp = mp_create(emu->r);
  emu->nk = nk_create(emu->r);

  /* create video renderer */
  if (emu->multi_threaded) {
    emu->pending_mutex = mutex_create();
    emu->pending_cond = cond_create();
    emu->r2 = r_create_from(emu->r);
  }

  struct render_backend *r2 = emu_video_renderer(emu);

  r_make_current(r2);

  emu->tr = tr_create(r2, provider);

  emu->video_fb_width = emu->video_width;
  emu->video_fb_height = emu->video_height;
  emu->video_fb = r_create_framebuffer(r2, emu->video_fb_width,
                                       emu->video_fb_height, &emu->video_tex);

  /* startup video thread */
  if (emu->multi_threaded) {
    emu->video_thread = thread_create(&emu_video_thread, NULL, emu);
    CHECK_NOTNULL(emu->video_thread);
  }

  /* make primary renderer active for the current thread */
  r_make_current(emu->r);
}

static void emu_video_resized(void *userdata) {
  struct emu *emu = userdata;

  emu->video_width = video_width(emu->host);
  emu->video_height = video_height(emu->host);
}

void emu_run_frame(struct emu *emu) {
  static const int64_t MACHINE_STEP = HZ_TO_NANO(1000);

  emu->video_state = FRAME_WAITING;

  while (emu->video_state != FRAME_RENDERED) {
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
  dc_destroy(emu->dc);

  free(emu);
}

struct emu *emu_create(struct host *host) {
  struct emu *emu = calloc(1, sizeof(struct emu));

  /* save off initial video size */
  emu->video_width = video_width(host);
  emu->video_height = video_height(host);

  /* setup host, bind event callbacks */
  emu->host = host;
  emu->host->userdata = emu;
  emu->host->video_resized = &emu_video_resized;
  emu->host->video_context_reset = &emu_video_context_reset;
  emu->host->video_context_destroyed = &emu_video_context_destroyed;
  emu->host->input_keydown = &emu_input_keydown;
  emu->host->input_mousemove = &emu_input_mousemove;

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
