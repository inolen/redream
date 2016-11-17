#include "hw/dreamcast.h"
#include "core/option.h"
#include "core/string.h"
#include "hw/aica/aica.h"
#include "hw/arm7/arm7.h"
#include "hw/debugger.h"
#include "hw/gdrom/gdrom.h"
#include "hw/holly/holly.h"
#include "hw/maple/maple.h"
#include "hw/memory.h"
#include "hw/pvr/pvr.h"
#include "hw/pvr/ta.h"
#include "hw/rom/boot.h"
#include "hw/rom/flash.h"
#include "hw/scheduler.h"
#include "hw/sh4/sh4.h"

DEFINE_OPTION_BOOL(gdb, false, "Run gdb debug server");

void device_run(struct device *dev, int64_t ns) {
  dev->execute_if->run(dev, ns);
}

bool dc_init(struct dreamcast *dc) {
  if (dc->debugger && !debugger_init(dc->debugger)) {
    dc_destroy(dc);
    return false;
  }

  if (!memory_init(dc->memory)) {
    dc_destroy(dc);
    return false;
  }

  /* initialize each device */
  list_for_each_entry(dev, &dc->devices, struct device, it) {
    /* cache references to other devices */
    dev->debugger = dc->debugger;
    dev->memory = dc->memory;
    dev->scheduler = dc->scheduler;
    dev->sh4 = dc->sh4;
    dev->arm = dc->arm;
    dev->aica = dc->aica;
    dev->boot = dc->boot;
    dev->flash = dc->flash;
    dev->gdrom = dc->gdrom;
    dev->holly = dc->holly;
    dev->maple = dc->maple;
    dev->pvr = dc->pvr;
    dev->ta = dc->ta;

    if (!dev->init(dev)) {
      dc_destroy(dc);
      return false;
    }
  }

  return true;
}

void dc_suspend(struct dreamcast *dc) {
  dc->running = 0;
}

void dc_resume(struct dreamcast *dc) {
  dc->running = 1;
}

void dc_tick(struct dreamcast *dc, int64_t ns) {
  if (dc->debugger) {
    debugger_tick(dc->debugger);
  }

  if (dc->running) {
    scheduler_tick(dc->scheduler, ns);
  }
}

void dc_paint(struct dreamcast *dc) {
  list_for_each_entry(dev, &dc->devices, struct device, it) {
    if (dev->window_if && dev->window_if->paint) {
      dev->window_if->paint(dev);
    }
  }
}

void dc_paint_debug_menu(struct dreamcast *dc, struct nk_context *ctx) {
  list_for_each_entry(dev, &dc->devices, struct device, it) {
    if (dev->window_if && dev->window_if->paint_debug_menu) {
      dev->window_if->paint_debug_menu(dev, ctx);
    }
  }
}

void dc_keydown(struct dreamcast *dc, enum keycode code, int16_t value) {
  list_for_each_entry(dev, &dc->devices, struct device, it) {
    if (dev->window_if && dev->window_if->keydown) {
      dev->window_if->keydown(dev, code, value);
    }
  }
}

struct execute_interface *dc_create_execute_interface(device_run_cb run,
                                                      int running) {
  struct execute_interface *execute =
      calloc(1, sizeof(struct execute_interface));
  execute->run = run;
  execute->running = running;
  return execute;
}

void dc_destroy_execute_interface(struct execute_interface *execute) {
  free(execute);
}

struct memory_interface *dc_create_memory_interface(struct dreamcast *dc,
                                                    address_map_cb mapper) {
  struct memory_interface *memory = calloc(1, sizeof(struct memory_interface));
  memory->mapper = mapper;
  memory->space = as_create(dc);
  return memory;
}

void dc_destroy_memory_interface(struct memory_interface *memory) {
  as_destroy(memory->space);
  free(memory);
}

struct window_interface *dc_create_window_interface(
    device_paint_cb paint, device_paint_debug_menu_cb paint_debug_menu,
    device_keydown_cb keydown) {
  struct window_interface *window = calloc(1, sizeof(struct window_interface));
  window->paint = paint;
  window->paint_debug_menu = paint_debug_menu;
  window->keydown = keydown;
  return window;
}

void dc_destroy_window_interface(struct window_interface *window) {
  free(window);
}

void *dc_create_device(struct dreamcast *dc, size_t size, const char *name,
                       bool (*init)(struct device *dev)) {
  struct device *dev = calloc(1, size);

  dev->dc = dc;
  dev->name = name;
  dev->init = init;

  list_add(&dc->devices, &dev->it);

  return dev;
}

struct device *dc_get_device(struct dreamcast *dc, const char *name) {
  list_for_each_entry(dev, &dc->devices, struct device, it) {
    if (!strcmp(dev->name, name)) {
      return dev;
    }
  }

  return NULL;
}

void dc_destroy_device(struct device *dev) {
  list_remove(&dev->dc->devices, &dev->it);

  free(dev);
}

struct dreamcast *dc_create(struct video_backend *video) {
  struct dreamcast *dc = calloc(1, sizeof(struct dreamcast));

  dc->debugger = OPTION_gdb ? debugger_create(dc) : NULL;
  dc->memory = memory_create(dc);
  dc->scheduler = scheduler_create(dc);
  dc->sh4 = sh4_create(dc);
  dc->arm = arm7_create(dc);
  dc->aica = aica_create(dc);
  dc->boot = boot_create(dc);
  dc->flash = flash_create(dc);
  dc->gdrom = gdrom_create(dc);
  dc->holly = holly_create(dc);
  dc->maple = maple_create(dc);
  dc->pvr = pvr_create(dc);
  dc->ta = ta_create(dc, video);

  if (!dc_init(dc)) {
    dc_destroy(dc);
    return NULL;
  }

  return dc;
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
  arm7_destroy(dc->arm);
  sh4_destroy(dc->sh4);
  scheduler_destroy(dc->scheduler);
  memory_destroy(dc->memory);

  if (dc->debugger) {
    debugger_destroy(dc->debugger);
  }

  free(dc);
}
