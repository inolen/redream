#include "guest/dreamcast.h"
#include "core/core.h"
#include "guest/aica/aica.h"
#include "guest/arm7/arm7.h"
#include "guest/bios/bios.h"
#include "guest/debugger.h"
#include "guest/gdrom/gdrom.h"
#include "guest/holly/holly.h"
#include "guest/maple/maple.h"
#include "guest/memory.h"
#include "guest/pvr/pvr.h"
#include "guest/pvr/ta.h"
#include "guest/rom/boot.h"
#include "guest/rom/flash.h"
#include "guest/scheduler.h"
#include "guest/sh4/sh4.h"

void dc_vblank_out(struct dreamcast *dc) {
  if (!dc->vblank_out) {
    return;
  }

  dc->vblank_out(dc->userdata);
}

void dc_vblank_in(struct dreamcast *dc, int video_disabled) {
  if (!dc->vblank_in) {
    return;
  }

  dc->vblank_in(dc->userdata, video_disabled);
}

void dc_finish_render(struct dreamcast *dc) {
  if (!dc->finish_render) {
    return;
  }

  dc->finish_render(dc->userdata);
}

void dc_start_render(struct dreamcast *dc, struct ta_context *ctx) {
  if (!dc->start_render) {
    return;
  }

  dc->start_render(dc->userdata, ctx);
}

void dc_push_pixels(struct dreamcast *dc, const uint8_t *data, int w, int h) {
  if (!dc->push_pixels) {
    return;
  }

  dc->push_pixels(dc->userdata, data, w, h);
}

void dc_push_audio(struct dreamcast *dc, const int16_t *data, int frames) {
  if (!dc->push_audio) {
    return;
  }

  dc->push_audio(dc->userdata, data, frames);
}

void dc_destroy_device(struct device *dev) {
  list_remove(&dev->dc->devices, &dev->it);

  free(dev);
}

struct device *dc_get_device(struct dreamcast *dc, const char *name) {
  list_for_each_entry(dev, &dc->devices, struct device, it) {
    if (!strcmp(dev->name, name)) {
      return dev;
    }
  }
  return NULL;
}

void *dc_create_device(struct dreamcast *dc, size_t size, const char *name,
                       device_init_cb init, device_post_init_cb post_init) {
  struct device *dev = calloc(1, size);

  dev->dc = dc;
  dev->name = name;
  dev->init = init;
  dev->post_init = post_init;

  list_add(&dc->devices, &dev->it);

  return dev;
}

void dc_remove_serial_device(struct dreamcast *dc) {
  dc->serial = NULL;
}

void dc_add_serial_device(struct dreamcast *dc, struct serial *serial) {
  dc->serial = serial;
}

void dc_input(struct dreamcast *dc, int port, int button, int16_t value) {
  maple_handle_input(dc->maple, port, button, value);
}

void dc_tick(struct dreamcast *dc, int64_t ns) {
  if (dc->debugger) {
    debugger_tick(dc->debugger);
  }

  if (dc->running) {
    sched_tick(dc->sched, ns);
  }
}

void dc_resume(struct dreamcast *dc) {
  dc->running = 1;
}

void dc_suspend(struct dreamcast *dc) {
  dc->running = 0;
}

int dc_running(struct dreamcast *dc) {
  return dc->running;
}

static int dc_load_bin(struct dreamcast *dc, const char *path) {
  FILE *fp = fopen(path, "rb");
  if (!fp) {
    return 0;
  }

  fseek(fp, 0, SEEK_END);
  int size = ftell(fp);
  fseek(fp, 0, SEEK_SET);

  /* load to 0x0c010000 (area 3) which is where 1ST_READ.BIN is loaded to */
  uint8_t *data = mem_ram(dc->mem, 0x00010000);
  int n = (int)fread(data, sizeof(uint8_t), size, fp);
  fclose(fp);

  if (n != size) {
    LOG_WARNING("failed to read %s", path);
    return 0;
  }

  /* boot to bios bootstrap */
  sh4_reset(dc->sh4, 0x0c010000);
  dc_resume(dc);

  return 1;
}

static int dc_load_disc(struct dreamcast *dc, const char *path) {
  struct disc *disc = disc_create(path, 1);

  if (!disc) {
    return 0;
  }

  /* boot to bios bootstrap */
  gdrom_set_disc(dc->gdrom, disc);
  sh4_reset(dc->sh4, 0xa0000000);
  dc_resume(dc);

  return 1;
}

int dc_load(struct dreamcast *dc, const char *path) {
  if (!path) {
    LOG_INFO("dc_load no path supplied, loading bios");

    /* boot to bios bootstrap */
    sh4_reset(dc->sh4, 0xa0000000);
    dc_resume(dc);
    return 1;
  }

  LOG_INFO("dc_load path=%s", path);

  return dc_load_disc(dc, path) || dc_load_bin(dc, path);
}

int dc_init(struct dreamcast *dc) {
  if (dc->debugger && !debugger_init(dc->debugger)) {
    LOG_WARNING("dc_init failed to initialize debugger");
    return 0;
  }

  if (!mem_init(dc->mem)) {
    LOG_WARNING("dc_init failed to initialize shared memory");
    return 0;
  }

  /* initialize each device */
  list_for_each_entry(dev, &dc->devices, struct device, it) {
    if (dev->init && !dev->init(dev)) {
      LOG_WARNING("dc_init init callback failed for '%s'", dev->name);
      return 0;
    }
  }

  list_for_each_entry(dev, &dc->devices, struct device, it) {
    if (dev->post_init && !dev->post_init(dev)) {
      LOG_WARNING("dc_init post_init callback failed for '%s'", dev->name);
      return 0;
    }
  }

  return 1;
}

void dc_destroy(struct dreamcast *dc) {
  ta_destroy(dc->ta);
  pvr_destroy(dc->pvr);
  maple_destroy(dc->maple);
  holly_destroy(dc->holly);
  gdrom_destroy(dc->gdrom);
  flash_destroy(dc->flash);
  boot_destroy(dc->boot);
  aica_destroy(dc->aica);
  arm7_destroy(dc->arm7);
  sh4_destroy(dc->sh4);
  bios_destroy(dc->bios);
  sched_destroy(dc->sched);
  mem_destroy(dc->mem);
  if (dc->debugger) {
    debugger_destroy(dc->debugger);
  }

  free(dc);
}

struct dreamcast *dc_create() {
  struct dreamcast *dc = calloc(1, sizeof(struct dreamcast));

#ifndef NDEBUG
  dc->debugger = debugger_create(dc);
#endif
  dc->mem = mem_create(dc);
  dc->sched = sched_create(dc);
  dc->bios = bios_create(dc);
  dc->sh4 = sh4_create(dc);
  dc->arm7 = arm7_create(dc);
  dc->aica = aica_create(dc);
  dc->boot = boot_create(dc);
  dc->flash = flash_create(dc);
  dc->gdrom = gdrom_create(dc);
  dc->holly = holly_create(dc);
  dc->maple = maple_create(dc);
  dc->pvr = pvr_create(dc);
  dc->ta = ta_create(dc);

  int res = dc_init(dc);
  CHECK(res);

  return dc;
}
