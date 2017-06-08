/*
 * client code for the dreamcast machine
 *
 * acts as a middle man between the dreamcast guest and local host. the host
 * interface provides callbacks for user input events, window resize events,
 * etc. that need to be passed to the dreamcast, while the dreamcast interface
 * provides callbacks that push frames of video and audio data to be presented
 * on the host
 *
 * this code encapsulates the logic that would otherwise need to be duplicated
 * for each of the multiple host implementations (sdl, libretro, etc.)
 */

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
#include "hw/pvr/trace.h"
#include "hw/scheduler.h"
#include "hw/sh4/sh4.h"
#include "render/imgui.h"
#include "render/microprofile.h"
#include "render/nuklear.h"
#include "render/render_backend.h"
#include "sys/thread.h"
#include "sys/time.h"

DEFINE_AGGREGATE_COUNTER(frames);

struct emu_texture {
  struct tr_texture;
  struct emu *emu;
  struct list_node free_it;
  struct rb_node live_it;

  struct memory_watch *texture_watch;
  struct memory_watch *palette_watch;
  struct list_node modified_it;
  int modified;
};

struct emu {
  struct host *host;
  struct dreamcast *dc;

  int debug_menu;
  volatile int running;
  volatile int video_width;
  volatile int video_height;
  volatile int video_resized;

  struct render_backend *r, *r2;
  struct imgui *imgui;
  struct microprofile *mp;
  struct nuklear *nk;
  struct trace_writer *trace_writer;

  /* hosts which support creating multiple gl contexts will render video on
     a second thread. the original hardware rendered asynchronously as well,
     so many games use this time to perform additional cpu work. on many
     games this upwards of doubles the performance */
  int multi_threaded;
  thread_t video_thread;

  /* latest context from the dreamcast, ready to be presented */
  mutex_t pending_mutex;
  cond_t pending_cond;
  unsigned pending_id;
  struct tile_context *pending_ctx;
  struct tr_context pending_rc;

  /* offscreen framebuffer the video output is rendered to */
  framebuffer_handle_t video_fb;
  texture_handle_t video_tex;
  sync_handle_t video_sync;
  volatile unsigned video_id;
  volatile unsigned last_video_id;

  /* texture cache. the dreamcast interface calls into us when new contexts are
     available to be rendered. parsing the contexts, uploading their textures to
     the render backend, and managing the texture cache is our responsibility */
  struct emu_texture textures[8192];
  struct list free_textures;
  struct rb_tree live_textures;

  /* textures for the current context are uploaded to the render backend by the
     video thread in parallel to the main thread executing. this is normally
     safe, as the real hardware rendered asynchronously as well.  unfortunately,
     some games will be naughty and modify a texture before receiving the end of
     render interrupt. in order to avoid race conditions around accessing the
     texture's dirty state, textures are not immediately marked dirty by the
     emulation thread when modified. instead, they are added to this modified
     list which will be processed the next time the threads are synchronized */
  struct list modified_textures;
};

/*
 * texture cache
 */
static int emu_texture_cmp(const struct rb_node *rb_lhs,
                           const struct rb_node *rb_rhs) {
  const struct emu_texture *lhs =
      rb_entry(rb_lhs, const struct emu_texture, live_it);
  tr_texture_key_t lhs_key = tr_texture_key(lhs->tsp, lhs->tcw);

  const struct emu_texture *rhs =
      rb_entry(rb_rhs, const struct emu_texture, live_it);
  tr_texture_key_t rhs_key = tr_texture_key(rhs->tsp, rhs->tcw);

  if (lhs_key < rhs_key) {
    return -1;
  } else if (lhs_key > rhs_key) {
    return 1;
  } else {
    return 0;
  }
}

static struct rb_callbacks emu_texture_cb = {&emu_texture_cmp, NULL, NULL};

static void emu_clear_textures(struct emu *emu) {
  LOG_INFO("emu_clear_textures");

  struct rb_node *it = rb_first(&emu->live_textures);

  while (it) {
    struct rb_node *next = rb_next(it);
    struct emu_texture *tex = rb_entry(it, struct emu_texture, live_it);

    tex->dirty = 1;

    it = next;
  }
}

static void emu_dirty_modified_textures(struct emu *emu) {
  list_for_each_entry(tex, &emu->modified_textures, struct emu_texture,
                      modified_it) {
    tex->dirty = 1;
    tex->modified = 0;
  }

  list_clear(&emu->modified_textures);
}

static void emu_texture_modified(const struct exception *ex, void *data) {
  struct emu_texture *tex = data;
  tex->texture_watch = NULL;

  if (!tex->modified) {
    list_add(&tex->emu->modified_textures, &tex->modified_it);
    tex->modified = 1;
  }
}

static void emu_palette_modified(const struct exception *ex, void *data) {
  struct emu_texture *tex = data;
  tex->palette_watch = NULL;

  if (!tex->modified) {
    list_add(&tex->emu->modified_textures, &tex->modified_it);
    tex->modified = 1;
  }
}

static struct emu_texture *emu_alloc_texture(struct emu *emu, union tsp tsp,
                                             union tcw tcw) {
  /* remove from free list */
  struct emu_texture *tex =
      list_first_entry(&emu->free_textures, struct emu_texture, free_it);
  CHECK_NOTNULL(tex);
  list_remove(&emu->free_textures, &tex->free_it);

  /* reset tex */
  memset(tex, 0, sizeof(*tex));
  tex->emu = emu;
  tex->tsp = tsp;
  tex->tcw = tcw;

  /* add to live tree */
  rb_insert(&emu->live_textures, &tex->live_it, &emu_texture_cb);

  return tex;
}

static struct tr_texture *emu_find_texture(void *userdata, union tsp tsp,
                                           union tcw tcw) {
  struct emu *emu = userdata;

  struct emu_texture search;
  search.tsp = tsp;
  search.tcw = tcw;

  struct emu_texture *tex =
      rb_find_entry(&emu->live_textures, &search, struct emu_texture, live_it,
                    &emu_texture_cb);
  return (struct tr_texture *)tex;
}

static void emu_register_texture_source(struct emu *emu, union tsp tsp,
                                        union tcw tcw) {
  struct emu_texture *entry =
      (struct emu_texture *)emu_find_texture(emu, tsp, tcw);

  if (!entry) {
    entry = emu_alloc_texture(emu, tsp, tcw);
    entry->dirty = 1;
  }

  /* mark texture source valid for the current pending frame */
  int first_registration_this_frame = entry->frame != emu->pending_id;
  entry->frame = emu->pending_id;

  /* set texture address */
  if (!entry->texture || !entry->palette) {
    ta_texture_info(emu->dc->ta, tsp, tcw, &entry->texture,
                    &entry->texture_size, &entry->palette,
                    &entry->palette_size);
  }

#ifdef NDEBUG
  /* add write callback in order to invalidate on future writes. the callback
     address will be page aligned, therefore it will be triggered falsely in
     some cases. over invalidate in these cases */
  if (!entry->texture_watch) {
    entry->texture_watch = add_single_write_watch(
        entry->texture, entry->texture_size, &emu_texture_modified, entry);
  }

  if (entry->palette && !entry->palette_watch) {
    entry->palette_watch = add_single_write_watch(
        entry->palette, entry->palette_size, &emu_palette_modified, entry);
  }
#endif

  if (emu->trace_writer && entry->dirty && first_registration_this_frame) {
    trace_writer_insert_texture(emu->trace_writer, tsp, tcw, entry->frame,
                                entry->palette, entry->palette_size,
                                entry->texture, entry->texture_size);
  }
}

static void emu_register_texture_sources(struct emu *emu,
                                         struct tile_context *ctx) {
  const uint8_t *data = ctx->params;
  const uint8_t *end = ctx->params + ctx->size;
  int vertex_type = 0;

  while (data < end) {
    union pcw pcw = *(union pcw *)data;

    switch (pcw.para_type) {
      case TA_PARAM_POLY_OR_VOL:
      case TA_PARAM_SPRITE: {
        const union poly_param *param = (const union poly_param *)data;

        vertex_type = ta_get_vert_type(param->type0.pcw);

        if (param->type0.pcw.texture) {
          emu_register_texture_source(emu, param->type0.tsp, param->type0.tcw);
        }
      } break;

      default:
        break;
    }

    data += ta_get_param_size(pcw, vertex_type);
  }
}

static void emu_init_textures(struct emu *emu) {
  for (int i = 0; i < array_size(emu->textures); i++) {
    struct emu_texture *tex = &emu->textures[i];
    list_add(&emu->free_textures, &tex->free_it);
  }
}

/*
 * trace recording
 */
static void emu_toggle_tracing(struct emu *emu) {
  if (!emu->trace_writer) {
    char filename[PATH_MAX];
    get_next_trace_filename(filename, sizeof(filename));

    emu->trace_writer = trace_writer_open(filename);

    if (!emu->trace_writer) {
      LOG_INFO("Failed to start tracing");
      return;
    }

    /* clear texture cache in order to generate insert events for all
       textures referenced while tracing */
    emu_clear_textures(emu);

    LOG_INFO("Begin tracing to %s", filename);
  } else {
    trace_writer_close(emu->trace_writer);
    emu->trace_writer = NULL;

    LOG_INFO("End tracing");
  }
}

/*
 * video rendering. responsible for dequeuing the latest raw tile_context from
 * the dreamcast, converting it into a renderable tr_context, and then rendering
 * and presenting it
 */
static struct render_backend *emu_video_renderer(struct emu *emu) {
  /* when using multi-threaded rendering, the video thread has its own render
     backend instance */
  return emu->multi_threaded ? emu->r2 : emu->r;
}

static void emu_render_frame(struct emu *emu) {
  struct render_backend *r2 = emu_video_renderer(emu);

  prof_counter_add(COUNTER_frames, 1);

  /* resize the framebuffer at this time if the output size has changed */
  if (emu->video_resized) {
    r_destroy_framebuffer(r2, emu->video_fb);

    emu->video_fb = r_create_framebuffer(r2, emu->video_width,
                                         emu->video_height, &emu->video_tex);
    emu->video_resized = 0;
  }

  /* render the current render context to the video framebuffer */
  framebuffer_handle_t original = r_get_framebuffer(r2);
  r_bind_framebuffer(r2, emu->video_fb);
  r_viewport(emu->r, emu->video_width, emu->video_height);
  tr_render_context(r2, &emu->pending_rc);
  r_bind_framebuffer(r2, original);

  /* insert fence for main thread to synchronize on in order to ensure that
     the context has completely rendered */
  if (emu->multi_threaded) {
    emu->video_sync = r_insert_sync(r2);
  }

  /* mark the currently available frame for emu_run_frame */
  emu->video_id = emu->pending_id;

  /* update frame-based profiler stats */
  prof_flip();
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

    /* convert the context, uploading its textures to the render backend */
    tr_convert_context(emu->r2, emu, &emu_find_texture, emu->pending_ctx,
                       &emu->pending_rc);
    emu->pending_ctx = NULL;

    /* render the parsed context to an offscreen framebuffer */
    emu_render_frame(emu);

    /* after the context has been rendered, release the mutex to unblock
       emu_guest_finish_render

       note, the main purpose of this mutex is to prevent the cpu from writing
       to texture memory before it's been uploaded to the render backend. from
       that perspective, this mutex could be released after tr_convert_context
       and before emu_render_frame, enabling the frame to take more time to
       render on the host than estimated by emu_guest_finish_render. however,
       then multiple framebuffers would have to be managed and synchronized in
       order to allow emu_render_frame and emu_paint to run in parallel */
    mutex_unlock(emu->pending_mutex);
  }

  return NULL;
}

static void emu_paint(struct emu *emu) {
  int width = emu->video_width;
  int height = emu->video_height;
  float fwidth = (float)emu->video_width;
  float fheight = (float)emu->video_height;

  imgui_update_input(emu->imgui);
  nk_update_input(emu->nk);

  r_viewport(emu->r, emu->video_width, emu->video_height);

  /* present the latest frame from the video thread */
  {
    struct ui_vertex verts[6] = {
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

    struct ui_surface quad = {0};
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

    r_begin_ui_surfaces(emu->r, verts, 6, NULL, 0);
    r_draw_ui_surface(emu->r, &quad);
    r_end_ui_surfaces(emu->r);
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

      /* add our own debug menu */
      nk_layout_row_push(ctx, 50.0f);

      if (nk_menu_begin_label(ctx, "DEBUG", NK_TEXT_CENTERED,
                              nk_vec2(160.0f, 200.0f))) {
        nk_layout_row_dynamic(ctx, DEBUG_MENU_HEIGHT, 1);

        if (!emu->trace_writer && nk_button_label(ctx, "start trace")) {
          emu_toggle_tracing(emu);
        } else if (emu->trace_writer && nk_button_label(ctx, "stop trace")) {
          emu_toggle_tracing(emu);
        }

        if (nk_button_label(ctx, "clear texture cache")) {
          emu_clear_textures(emu);
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

      int remaining_width =
          ctx->current->layout->bounds.w -
          ctx->current->layout->row.item_offset -
          ctx->current->layout->row.index * ctx->style.window.spacing.x - 4.0f;
      nk_layout_row_push(ctx, remaining_width);
      nk_label(ctx, status, NK_TEXT_RIGHT);

      nk_layout_row_end(ctx);
      nk_menubar_end(ctx);
    }
    nk_end(ctx);
  }

  imgui_render(emu->imgui);
  mp_render(emu->mp);
  nk_render(emu->nk);

  /* mark the last rendered video id for emu_run_frame */
  emu->last_video_id = emu->video_id;
}

/*
 * dreamcast guest interface
 */
static void emu_guest_poll_input(void *userdata) {
  struct emu *emu = userdata;

  input_poll(emu->host);
}

static void emu_guest_finish_render(void *userdata) {
  struct emu *emu = userdata;

  if (emu->multi_threaded) {
    /* ideally, the video thread has parsed the pending context, uploaded its
       textures, etc. during the estimated render time. however, if it hasn't
       finished, the emulation thread must be paused to avoid altering
       the yet-to-be-uploaded texture memory */
    mutex_lock(emu->pending_mutex);

    /* if pending_ctx is non-NULL here, a frame is being skipped */
    emu->pending_ctx = NULL;

    mutex_unlock(emu->pending_mutex);
  }
}

static void emu_guest_start_render(void *userdata, struct tile_context *ctx) {
  struct emu *emu = userdata;

  /* note, while the video thread is guaranteed to not to be touching texture
     memory from the previous frame at this point, it could still be actually
     rendering the previous frame */

  /* incement internal frame number. this frame number is assigned to the each
     texture source registered to assert synchronization between the emulator
     and video thread is working as expected */
  emu->pending_id++;

  /* now that the video thread is sure to not be accessing the texture data,
     mark any textures dirty that were invalidated by a memory watch */
  emu_dirty_modified_textures(emu);

  /* register the source of each texture referenced by the context with the
     tile renderer. note, uploading the texture to the render backend happens
     lazily while converting the context. this registration just lets the
     backend know where the texture's source data is */
  emu_register_texture_sources(emu, ctx);

  if (emu->trace_writer) {
    trace_writer_render_context(emu->trace_writer, ctx);
  }

  if (emu->multi_threaded) {
    /* save off context and notify video thread that it's available */
    mutex_lock(emu->pending_mutex);

    emu->pending_ctx = ctx;
    cond_signal(emu->pending_cond);

    mutex_unlock(emu->pending_mutex);
  } else {
    /* convert the context and immediately render it */
    tr_convert_context(emu->r, emu, &emu_find_texture, ctx, &emu->pending_rc);

    emu_render_frame(emu);
  }
}

static void emu_guest_push_audio(void *userdata, const int16_t *data,
                                 int frames) {
  struct emu *emu = userdata;
  audio_push(emu->host, data, frames);
}

/*
 * local host interface
 */
static void emu_host_mousemove(void *userdata, int port, int x, int y) {
  struct emu *emu = userdata;

  imgui_mousemove(emu->imgui, x, y);
  mp_mousemove(emu->mp, x, y);
  nk_mousemove(emu->nk, x, y);
}

static void emu_host_keydown(void *userdata, int port, enum keycode key,
                             int16_t value) {
  struct emu *emu = userdata;

  if (key == K_F1 && value > 0) {
    emu->debug_menu = emu->debug_menu ? 0 : 1;
  } else {
    imgui_keydown(emu->imgui, key, value);
    mp_keydown(emu->mp, key, value);
    nk_keydown(emu->nk, key, value);
  }

  if (key >= K_CONT_C && key <= K_CONT_RTRIG) {
    dc_input(emu->dc, port, key - K_CONT_C, value);
  }
}

static void emu_host_context_destroyed(void *userdata) {
  struct emu *emu = userdata;

  if (!emu->running) {
    return;
  }

  emu->running = 0;

  /* destroy the video thread */
  if (emu->multi_threaded) {
    mutex_lock(emu->pending_mutex);
    emu->pending_ctx = (struct tile_context *)0xdeadbeef;
    cond_signal(emu->pending_cond);
    mutex_unlock(emu->pending_mutex);

    void *result;
    thread_join(emu->video_thread, &result);
  }

  /* reset texture cache */
  emu_clear_textures(emu);

  /* destroy video renderer */
  struct render_backend *r2 = emu_video_renderer(emu);

  r_make_current(r2);

  r_destroy_framebuffer(r2, emu->video_fb);
  if (emu->video_sync) {
    r_destroy_sync(r2, emu->video_sync);
  }

  if (emu->multi_threaded) {
    r_destroy(emu->r2);
    cond_destroy(emu->pending_cond);
    mutex_destroy(emu->pending_mutex);
  }

  /* destroy primary renderer */
  r_make_current(emu->r);

  nk_destroy(emu->nk);
  mp_destroy(emu->mp);
  imgui_destroy(emu->imgui);
  r_destroy(emu->r);
}

static void emu_host_context_reset(void *userdata) {
  struct emu *emu = userdata;

  emu_host_context_destroyed(userdata);

  emu->running = 1;

  /* create primary renderer */
  emu->r = r_create(emu->host);
  emu->imgui = imgui_create(emu->r);
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

  emu->video_fb = r_create_framebuffer(r2, emu->video_width, emu->video_height,
                                       &emu->video_tex);

  /* startup video thread */
  if (emu->multi_threaded) {
    emu->video_thread = thread_create(&emu_video_thread, NULL, emu);
    CHECK_NOTNULL(emu->video_thread);
  }

  /* make primary renderer active for the current thread */
  r_make_current(emu->r);
}

static void emu_host_resized(void *userdata) {
  struct emu *emu = userdata;

  emu->video_width = video_width(emu->host);
  emu->video_height = video_height(emu->host);
  emu->video_resized = 1;
}

void emu_run_frame(struct emu *emu) {
  static const int64_t MACHINE_STEP = HZ_TO_NANO(1000);

  while (emu->video_id <= emu->last_video_id) {
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
  emu->host->video_resized = &emu_host_resized;
  emu->host->video_context_reset = &emu_host_context_reset;
  emu->host->video_context_destroyed = &emu_host_context_destroyed;
  emu->host->input_keydown = &emu_host_keydown;
  emu->host->input_mousemove = &emu_host_mousemove;

  /* create dreamcast, bind client callbacks */
  emu->dc = dc_create();
  emu->dc->userdata = emu;
  emu->dc->push_audio = &emu_guest_push_audio;
  emu->dc->start_render = &emu_guest_start_render;
  emu->dc->finish_render = &emu_guest_finish_render;
  emu->dc->poll_input = &emu_guest_poll_input;

  /* start up the video thread */
  emu->multi_threaded = video_gl_supports_multiple_contexts(emu->host);

  /* enable debug menu by default */
  emu->debug_menu = 1;

  emu_init_textures(emu);

  return emu;
}
