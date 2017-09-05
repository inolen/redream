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
#include "core/thread.h"
#include "core/time.h"
#include "file/trace.h"
#include "guest/aica/aica.h"
#include "guest/arm7/arm7.h"
#include "guest/bios/bios.h"
#include "guest/dreamcast.h"
#include "guest/gdrom/gdrom.h"
#include "guest/holly/holly.h"
#include "guest/maple/maple.h"
#include "guest/memory.h"
#include "guest/pvr/pvr.h"
#include "guest/pvr/ta.h"
#include "guest/pvr/tr.h"
#include "guest/scheduler.h"
#include "guest/sh4/sh4.h"
#include "host/host.h"
#include "render/imgui.h"
#include "render/render_backend.h"

DEFINE_AGGREGATE_COUNTER(frames);

DEFINE_PERSISTENT_OPTION_INT(debug, 1, "Show debug menu");
DEFINE_PERSISTENT_OPTION_STRING(aspect, "stretch", "Video aspect ratio");

enum {
  ASPECT_RATIO_STRETCH,
  ASPECT_RATIO_16BY9,
  ASPECT_RATIO_4BY3,
};

static const char *aspect_ratios[] = {
    "stretch", "16:9", "4:3",
};

/* emulation thread state */
enum {
  EMU_SHUTDOWN,
  EMU_WAITING,
  EMU_RUNFRAME,
};

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

  struct render_backend *r;
  struct trace_writer *trace_writer;
  int aspect_ratio;

  /* when running with multiple threads, the dreamcast emulation is ran on a
     secondary thread, in order to allow rendering to be done asynchronously on
     the main thread. the original hardware rendered asynchronously, so many
     games use this time to perform additional cpu work. on some games this
     upwards of doubles the performance */
  int multi_threaded;

  /* emulation thread synchronization primitives */
  volatile int state;
  volatile unsigned frame;
  thread_t run_thread;
  mutex_t run_mutex;
  cond_t run_cond;
  mutex_t frame_mutex;
  cond_t frame_cond;

  /* latest context from the dreamcast, ready to be rendered */
  struct ta_context *pending_ctx;
  struct tr_context pending_rc;
  unsigned pending_id;

  /* texture cache. the dreamcast interface calls into us when new contexts are
     available to be rendered. parsing the contexts, uploading their textures to
     the render backend, and managing the texture cache is our responsibility */
  struct emu_texture textures[8192];
  struct list free_textures;
  struct rb_tree live_textures;

  /* textures for the current context are uploaded to the render backend by the
     video thread in parallel to the emulation thread executing. normally, this
     is safe as the real hardware also rendered asynchronously. unfortunately,
     some games will be naughty and modify a texture before receiving the end of
     render interrupt. in order to avoid race conditions around accessing the
     texture's dirty state, textures are not immediately marked dirty by the
     emulation thread when modified. instead, they are added to this modified
     list which will be processed the next time the threads are synchronized */
  struct list modified_textures;

  /* debug stats */
  int frame_stats;
  float swap_times[360];
  int64_t last_swap;
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

static void emu_dirty_textures(struct emu *emu) {
  LOG_INFO("emu_dirty_textures");

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

static void emu_texture_modified(const struct exception_state *ex, void *data) {
  struct emu_texture *tex = data;
  tex->texture_watch = NULL;

  if (!tex->modified) {
    list_add(&tex->emu->modified_textures, &tex->modified_it);
    tex->modified = 1;
  }
}

static void emu_palette_modified(const struct exception_state *ex, void *data) {
  struct emu_texture *tex = data;
  tex->palette_watch = NULL;

  if (!tex->modified) {
    list_add(&tex->emu->modified_textures, &tex->modified_it);
    tex->modified = 1;
  }
}

static void emu_free_texture(struct emu *emu, struct emu_texture *tex) {
  /* remove from live tree */
  rb_unlink(&emu->live_textures, &tex->live_it, &emu_texture_cb);

  /* add back to free list */
  list_add(&emu->free_textures, &tex->free_it);
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
                                         struct ta_context *ctx) {
  const uint8_t *data = ctx->params;
  const uint8_t *end = ctx->params + ctx->size;
  int vert_type = 0;

  while (data < end) {
    union pcw pcw = *(union pcw *)data;

    switch (pcw.para_type) {
      case TA_PARAM_POLY_OR_VOL:
      case TA_PARAM_SPRITE: {
        const union poly_param *param = (const union poly_param *)data;

        vert_type = ta_vert_type(param->type0.pcw);

        if (param->type0.pcw.texture) {
          emu_register_texture_source(emu, param->type0.tsp, param->type0.tcw);
        }
      } break;

      default:
        break;
    }

    data += ta_param_size(pcw, vert_type);
  }
}

/*
 * trace recording
 */
static void emu_stop_tracing(struct emu *emu) {
  if (!emu->trace_writer) {
    return;
  }

  trace_writer_close(emu->trace_writer);
  emu->trace_writer = NULL;

  LOG_INFO("end tracing");
}

static void emu_start_tracing(struct emu *emu) {
  if (emu->trace_writer) {
    return;
  }

  char filename[PATH_MAX];
  get_next_trace_filename(filename, sizeof(filename));

  emu->trace_writer = trace_writer_open(filename);

  if (!emu->trace_writer) {
    LOG_INFO("failed to start tracing");
    return;
  }

  /* clear texture cache in order to generate insert events for all
     textures referenced while tracing */
  emu_dirty_textures(emu);

  LOG_INFO("begin tracing to %s", filename);
}

/*
 * dreamcast guest interface
 */
static void emu_guest_vertical_blank(void *userdata) {
  struct emu *emu = userdata;

  emu->frame++;
}

static void emu_guest_finish_render(void *userdata) {
  struct emu *emu = userdata;

  if (emu->multi_threaded) {
    /* ideally, the video thread has parsed the pending context, uploaded its
       textures, etc. during the estimated render time. however, if it hasn't
       finished, the emulation thread must be paused to avoid altering
       the yet-to-be-uploaded texture memory */
    mutex_lock(emu->frame_mutex);

    /* if pending_ctx is non-NULL here, a frame is being skipped */
    emu->pending_ctx = NULL;
    cond_signal(emu->frame_cond);

    mutex_unlock(emu->frame_mutex);
  }
}

static void emu_guest_start_render(void *userdata, struct ta_context *ctx) {
  struct emu *emu = userdata;

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
    mutex_lock(emu->frame_mutex);

    emu->pending_ctx = ctx;
    cond_signal(emu->frame_cond);

    mutex_unlock(emu->frame_mutex);
  } else {
    /* convert the context and immediately render it */
    tr_convert_context(emu->r, emu, &emu_find_texture, ctx, &emu->pending_rc);
    tr_render_context(emu->r, &emu->pending_rc);
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
static void emu_host_keydown(void *userdata, int port, enum keycode key,
                             int16_t value) {
  struct emu *emu = userdata;

  if (key == K_F1 && value > 0) {
    OPTION_debug = !OPTION_debug;
  }

  if (key >= K_CONT_C && key <= K_CONT_RTRIG) {
    dc_input(emu->dc, port, key - K_CONT_C, value);
  }
}

static void emu_host_video_swapped(void *userdata) {
  struct emu *emu = userdata;

  /* keep track of the time between swaps */
  int64_t now = time_nanoseconds();

  if (emu->last_swap) {
    float swap_time_ms = (float)(now - emu->last_swap) / 1000000.0f;
    int num_swap_times = ARRAY_SIZE(emu->swap_times);
    emu->swap_times[emu->frame % num_swap_times] = swap_time_ms;
  }

  emu->last_swap = now;
}

static void emu_host_video_destroyed(void *userdata) {
  struct emu *emu = userdata;

  rb_for_each_entry_safe(tex, &emu->live_textures, struct emu_texture,
                         live_it) {
    r_destroy_texture(emu->r, tex->handle);
    emu_free_texture(emu, tex);
  }

  emu->r = NULL;
}

static void emu_host_video_created(void *userdata, struct render_backend *r) {
  struct emu *emu = userdata;

  emu->r = r;
}

/*
 * options
 */
static void emu_set_aspect_ratio(struct emu *emu, const char *new_ratio) {
  int i;
  int num_aspect_ratios = ARRAY_SIZE(aspect_ratios);

  for (i = 0; i < num_aspect_ratios; i++) {
    const char *aspect_ratio = aspect_ratios[i];

    if (!strcmp(aspect_ratio, new_ratio)) {
      break;
    }
  }

  /* force to stretch if the new ratio isn't a valid one */
  if (i == num_aspect_ratios) {
    i = ASPECT_RATIO_STRETCH;
  }

  /* update persistent option as well as this session's aspect ratio */
  strncpy(OPTION_aspect, aspect_ratios[i], sizeof(OPTION_aspect));
  emu->aspect_ratio = i;

  /* if a widescreen hack is enabled, force to stretch for the session */
  if (gdrom_widescreen_enabled(emu->dc->gdrom)) {
    emu->aspect_ratio = ASPECT_RATIO_16BY9;
  }
}

static void emu_debug_menu(struct emu *emu) {
#ifdef HAVE_IMGUI
  if (!OPTION_debug) {
    return;
  }

  if (igBeginMainMenuBar()) {
    if (igBeginMenu("DEBUG", 1)) {
      if (igMenuItem("frame stats", NULL, emu->frame_stats, 1)) {
        emu->frame_stats = !emu->frame_stats;
      }
      if (!emu->trace_writer && igMenuItem("start trace", NULL, 0, 1)) {
        emu_start_tracing(emu);
      }
      if (emu->trace_writer && igMenuItem("stop trace", NULL, 1, 1)) {
        emu_stop_tracing(emu);
      }
      if (igMenuItem("clear texture cache", NULL, 0, 1)) {
        emu_dirty_textures(emu);
      }
      igEndMenu();
    }

    if (igBeginMenu("VIDEO", 1)) {
      if (igBeginMenu("aspect ratio", 1)) {
        for (int i = 0; i < ARRAY_SIZE(aspect_ratios); i++) {
          const char *aspect_ratio = aspect_ratios[i];
          int selected = !strcmp(OPTION_aspect, aspect_ratio);

          if (igMenuItem(aspect_ratio, NULL, selected, 1)) {
            emu_set_aspect_ratio(emu, aspect_ratio);
          }
        }

        igEndMenu();
      }
      if (video_can_fullscreen(emu->host)) {
        int fullscreen = video_is_fullscreen(emu->host);
        if (igMenuItem("fullscreen", NULL, fullscreen, 1)) {
          video_set_fullscreen(emu->host, !fullscreen);
        }
      }
      igEndMenu();
    }

    igEndMainMenuBar();
  }

  bios_debug_menu(emu->dc->bios);
  gdrom_debug_menu(emu->dc->gdrom);
  holly_debug_menu(emu->dc->holly);
  aica_debug_menu(emu->dc->aica);
  sh4_debug_menu(emu->dc->sh4);

  /* add status */
  if (igBeginMainMenuBar()) {
    char status[128];
    int frames = (int)prof_counter_load(COUNTER_frames);
    int ta_renders = (int)prof_counter_load(COUNTER_ta_renders);
    int pvr_vblanks = (int)prof_counter_load(COUNTER_pvr_vblanks);
    int sh4_instrs = (int)(prof_counter_load(COUNTER_sh4_instrs) / 1000000.0f);
    int arm7_instrs =
        (int)(prof_counter_load(COUNTER_arm7_instrs) / 1000000.0f);

    snprintf(status, sizeof(status), "FPS %3d RPS %3d VBS %3d SH4 %4d ARM %d",
             frames, ta_renders, pvr_vblanks, sh4_instrs, arm7_instrs);

    /* right align */
    struct ImVec2 content;
    struct ImVec2 size;
    igGetContentRegionMax(&content);
    igCalcTextSize(&size, status, NULL, 0, 0.0f);
    igSetCursorPosX(content.x - size.x);
    igText(status);

    igEndMainMenuBar();
  }

  if (emu->frame_stats) {
    bool opened = true;

    if (igBegin("frame stats", &opened, ImGuiWindowFlags_AlwaysAutoResize)) {
      /* swap times */
      {
        struct ImVec2 graph_size = {300.0f, 50.0f};
        int num_swap_times = ARRAY_SIZE(emu->swap_times);

        float min_time = FLT_MAX;
        float max_time = -FLT_MAX;
        float avg_time = 0.0f;
        for (int i = 0; i < num_swap_times; i++) {
          float time = emu->swap_times[i];
          min_time = MIN(min_time, time);
          max_time = MAX(max_time, time);
          avg_time += time;
        }
        avg_time /= num_swap_times;

        igValueFloat("min swap time", min_time, "%.2f");
        igValueFloat("max swap time", max_time, "%.2f");
        igValueFloat("avg swap time", avg_time, "%.2f");
        igPlotLines("", emu->swap_times, num_swap_times,
                    emu->frame % num_swap_times, NULL, 0.0f, 60.0f, graph_size,
                    sizeof(float));
      }
    }
    igEnd();

    emu->frame_stats = (int)opened;
  }
#endif
}

/*
 * frame running logic
 */
static void emu_run_frame(struct emu *emu);

static void *emu_run_thread(void *data) {
  struct emu *emu = data;

  while (1) {
    /* wait for video thread to request a frame to be ran */
    {
      mutex_lock(emu->run_mutex);

      while (emu->state == EMU_WAITING) {
        cond_wait(emu->run_cond, emu->run_mutex);
      }

      if (emu->state == EMU_SHUTDOWN) {
        break;
      }

      emu_run_frame(emu);

      emu->state = EMU_WAITING;

      mutex_unlock(emu->run_mutex);
    }

    /* in case no context was submitted before the frame end, signal the video
       thread to continue, reusing the previous context */
    {
      mutex_lock(emu->frame_mutex);

      cond_signal(emu->frame_cond);

      mutex_unlock(emu->frame_mutex);
    }
  }

  return NULL;
}

static void emu_run_frame(struct emu *emu) {
  const int64_t MACHINE_STEP = HZ_TO_NANO(1000);
  unsigned start_frame = emu->frame;

  /* run up to the next vblank */
  while (emu->frame == start_frame) {
    dc_tick(emu->dc, MACHINE_STEP);
  }
}

void emu_render_frame(struct emu *emu, int width, int height) {
  prof_counter_add(COUNTER_frames, 1);

  /* check that we're not being called between calls to emu_host_video_destroyed
     and emu_host_video_created */
  CHECK_NOTNULL(emu->r);

  /* adjust dreamcast viewport based on aspect ratio */
  int frame_height, frame_width;
  int frame_x, frame_y;

  if (emu->aspect_ratio == ASPECT_RATIO_STRETCH) {
    frame_height = height;
    frame_width = width;
    frame_x = 0;
    frame_y = 0;
  } else if (emu->aspect_ratio == ASPECT_RATIO_16BY9) {
    frame_width = width;
    frame_height = (int)(frame_width * (9.0f / 16.0f));
    frame_x = 0;
    frame_y = (int)((height - frame_height) / 2.0f);
  } else if (emu->aspect_ratio == ASPECT_RATIO_4BY3) {
    frame_height = height;
    frame_width = (int)(frame_height * (4.0f / 3.0f));
    frame_x = (int)((width - frame_width) / 2.0f);
    frame_y = 0;
  } else {
    LOG_FATAL("unexpected aspect ratio %d", emu->aspect_ratio);
  }

  r_clear(emu->r);
  r_viewport(emu->r, frame_x, frame_y, frame_width, frame_height);

  if (emu->multi_threaded) {
    /* tell the emulation thread to run the next frame */
    {
      mutex_lock(emu->run_mutex);

      /* build the debug menus before running the frame, while the two threads
         are synchronized */
      emu_debug_menu(emu);

      emu->state = EMU_RUNFRAME;
      cond_signal(emu->run_cond);

      mutex_unlock(emu->run_mutex);
    }

    /* wait for the emulation thread to submit a context */
    {
      mutex_lock(emu->frame_mutex);

      while (emu->state == EMU_RUNFRAME && !emu->pending_ctx) {
        cond_wait(emu->frame_cond, emu->frame_mutex);
      }

      /* if a context was submitted before the vblank, convert it and upload
         its textures to the render backend */
      if (emu->pending_ctx) {
        tr_convert_context(emu->r, emu, &emu_find_texture, emu->pending_ctx,
                           &emu->pending_rc);
        emu->pending_ctx = NULL;
      }

      /* unblock the emulation thread */
      mutex_unlock(emu->frame_mutex);
    }

    /* render the latest context. note, the emulation thread may still be
       running at this time */
    tr_render_context(emu->r, &emu->pending_rc);
  } else {
    emu_debug_menu(emu);
    emu_run_frame(emu);
  }
}

int emu_load_game(struct emu *emu, const char *path) {
  if (!dc_load(emu->dc, path)) {
    return 0;
  }

  emu_set_aspect_ratio(emu, OPTION_aspect);

  dc_resume(emu->dc);

  return 1;
}

void emu_destroy(struct emu *emu) {
  /* shutdown the emulation thread */
  if (emu->multi_threaded) {
    {
      mutex_lock(emu->run_mutex);

      emu->state = EMU_SHUTDOWN;
      cond_signal(emu->run_cond);

      mutex_unlock(emu->run_mutex);
    }

    void *result;
    thread_join(emu->run_thread, &result);
  }

  emu_stop_tracing(emu);

  dc_destroy(emu->dc);

  free(emu);
}

struct emu *emu_create(struct host *host, struct render_backend *r) {
  struct emu *emu = calloc(1, sizeof(struct emu));

  emu->r = r;

  /* setup host, bind event callbacks */
  emu->host = host;
  emu->host->userdata = emu;
  emu->host->video_created = &emu_host_video_created;
  emu->host->video_destroyed = &emu_host_video_destroyed;
  emu->host->video_swapped = &emu_host_video_swapped;
  emu->host->input_keydown = &emu_host_keydown;

  /* create dreamcast, bind client callbacks */
  emu->dc = dc_create();
  emu->dc->userdata = emu;
  emu->dc->push_audio = &emu_guest_push_audio;
  emu->dc->start_render = &emu_guest_start_render;
  emu->dc->finish_render = &emu_guest_finish_render;
  emu->dc->vertical_blank = &emu_guest_vertical_blank;

  /* add all textures to free list by default */
  for (int i = 0; i < ARRAY_SIZE(emu->textures); i++) {
    struct emu_texture *tex = &emu->textures[i];
    list_add(&emu->free_textures, &tex->free_it);
  }

  /* enable the cpu / gpu to be emulated in parallel */
  emu->multi_threaded = 1;

  if (emu->multi_threaded) {
    emu->state = EMU_WAITING;
    emu->run_mutex = mutex_create();
    emu->run_cond = cond_create();
    emu->frame_mutex = mutex_create();
    emu->frame_cond = cond_create();

    emu->run_thread = thread_create(&emu_run_thread, NULL, emu);
    CHECK_NOTNULL(emu->run_thread);
  }

  return emu;
}
