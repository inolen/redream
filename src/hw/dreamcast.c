#include "core/option.h"
#include "core/string.h"
#include "hw/sh4/sh4.h"
#include "hw/arm/arm.h"
#include "hw/aica/aica.h"
#include "hw/gdrom/gdrom.h"
#include "hw/holly/holly.h"
#include "hw/holly/pvr.h"
#include "hw/holly/ta.h"
#include "hw/maple/maple.h"
#include "hw/dreamcast.h"
#include "hw/debugger.h"
#include "hw/memory.h"
#include "hw/scheduler.h"

DEFINE_OPTION_BOOL(gdb, false, "Run gdb debug server");

struct execute_interface *execute_interface_create(device_run_cb run) {
  struct execute_interface *execute =
      calloc(1, sizeof(struct execute_interface));
  execute->run = run;
  return execute;
}

void execute_interface_destroy(struct execute_interface *execute) {
  free(execute);
}

struct memory_interface *memory_interface_create(struct dreamcast *dc,
                                                 address_map_cb mapper) {
  struct memory_interface *memory = calloc(1, sizeof(struct memory_interface));
  memory->mapper = mapper;
  memory->space = as_create(dc);
  return memory;
}

void memory_interface_destroy(struct memory_interface *memory) {
  as_destroy(memory->space);
  free(memory);
}

struct window_interface *window_interface_create(device_paint_cb paint,
                                                 device_keydown_cb keydown) {
  struct window_interface *window = calloc(1, sizeof(struct window_interface));
  window->paint = paint;
  window->keydown = keydown;
  return window;
}

void window_interface_destroy(struct window_interface *window) {
  free(window);
}

void device_run(struct device *dev, int64_t ns) {
  dev->execute->run(dev, ns);
}

void *dc_create_device(struct dreamcast *dc, size_t size, const char *name,
                       device_init_cb init) {
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

bool dc_init(struct dreamcast *dc) {
  if (dc->debugger && !debugger_init(dc->debugger)) {
    dc_destroy(dc);
    return false;
  }

  if (!memory_init(dc->memory)) {
    dc_destroy(dc);
    return false;
  }

  // initialize each device
  list_for_each_entry(dev, &dc->devices, struct device, it) {
    if (!dev->init(dev)) {
      dc_destroy(dc);
      return false;
    }
  }

  return true;
}

void dc_suspend(struct dreamcast *dc) {
  dc->suspended = true;
}

void dc_resume(struct dreamcast *dc) {
  dc->suspended = false;
}

void dc_tick(struct dreamcast *dc, int64_t ns) {
  if (dc->debugger) {
    debugger_tick(dc->debugger);
  }

  if (!dc->suspended) {
    scheduler_tick(dc->scheduler, ns);
  }
}

void dc_paint(struct dreamcast *dc, bool show_main_menu) {
  list_for_each_entry(dev, &dc->devices, struct device, it) {
    if (dev->window && dev->window->paint) {
      dev->window->paint(dev, show_main_menu);
    }
  }
}

void dc_keydown(struct dreamcast *dc, enum keycode code, int16_t value) {
  list_for_each_entry(dev, &dc->devices, struct device, it) {
    if (dev->window && dev->window->keydown) {
      dev->window->keydown(dev, code, value);
    }
  }
}

struct dreamcast *dc_create(void *rb) {
  struct dreamcast *dc = calloc(1, sizeof(struct dreamcast));

  dc->debugger = OPTION_gdb ? debugger_create(dc) : NULL;
  dc->memory = memory_create(dc);
  dc->scheduler = scheduler_create(dc);
  dc->sh4 = sh4_create(dc);
  dc->arm = arm_create(dc);
  dc->aica = aica_create(dc);
  dc->holly = holly_create(dc);
  dc->gdrom = gdrom_create(dc);
  dc->maple = maple_create(dc);
  dc->pvr = pvr_create(dc);
  dc->ta = ta_create(dc, rb);

  if (!dc_init(dc)) {
    dc_destroy(dc);
    return NULL;
  }

  return dc;
}

void dc_destroy(struct dreamcast *dc) {
  if (dc->debugger) {
    debugger_destroy(dc->debugger);
  }

  memory_destroy(dc->memory);
  scheduler_destroy(dc->scheduler);
  sh4_destroy(dc->sh4);
  arm_destroy(dc->arm);
  aica_destroy(dc->aica);
  holly_destroy(dc->holly);
  gdrom_destroy(dc->gdrom);
  maple_destroy(dc->maple);
  pvr_destroy(dc->pvr);
  ta_destroy(dc->ta);

  free(dc);
}
