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

execute_interface_t *execute_interface_create(device_run_cb run) {
  execute_interface_t *execute = calloc(1, sizeof(execute_interface_t));
  execute->run = run;
  return execute;
}

void execute_interface_destroy(execute_interface_t *execute) {
  free(execute);
}

memory_interface_t *memory_interface_create(struct dreamcast_s *dc,
                                            address_map_cb mapper) {
  memory_interface_t *memory = calloc(1, sizeof(memory_interface_t));
  memory->mapper = mapper;
  memory->space = address_space_create(dc);
  return memory;
}

void memory_interface_destroy(memory_interface_t *memory) {
  address_space_destroy(memory->space);
  free(memory);
}

window_interface_t *window_interface_create(device_paint_cb paint,
                                            device_keydown_cb keydown) {
  window_interface_t *window = calloc(1, sizeof(window_interface_t));
  window->paint = paint;
  window->keydown = keydown;
  return window;
}

void window_interface_destroy(window_interface_t *window) {
  free(window);
}

void device_run(device_t *dev, int64_t ns) {
  dev->execute->run(dev, ns);
}

dreamcast_t *dc_create(void *rb) {
  dreamcast_t *dc = calloc(1, sizeof(dreamcast_t));

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

void dc_destroy(dreamcast_t *dc) {
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

bool dc_init(dreamcast_t *dc) {
  if (dc->debugger && !debugger_init(dc->debugger)) {
    dc_destroy(dc);
    return false;
  }

  if (!memory_init(dc->memory)) {
    dc_destroy(dc);
    return false;
  }

  // initialize each device
  device_t *dev = dc->devices;

  while (dev) {
    if (!dev->init(dev)) {
      dc_destroy(dc);
      return false;
    }

    dev = dev->next;
  }

  return true;
}

void dc_suspend(dreamcast_t *dc) {
  dc->suspended = true;
}

void dc_resume(dreamcast_t *dc) {
  dc->suspended = false;
}

void *dc_create_device(dreamcast_t *dc, size_t size, const char *name,
                       device_init_cb init) {
  device_t *dev = calloc(1, size);
  dev->dc = dc;
  dev->name = name;
  dev->init = init;

  // insert into device list
  dev->next = dc->devices;
  dc->devices = dev;

  return dev;
}

void dc_destroy_device(device_t *dev) {
  // remove from device list
  device_t **it = &dev->dc->devices;

  while (*it) {
    if (*it == dev) {
      *it = (*it)->next;
      break;
    }

    it = &(*it)->next;
  }

  free(dev);
}

device_t *dc_get_device(dreamcast_t *dc, const char *name) {
  device_t *dev = dc->devices;

  while (dev) {
    if (!strcmp(dev->name, name)) {
      return dev;
    }

    dev = dev->next;
  }

  return NULL;
}

void dc_tick(dreamcast_t *dc, int64_t ns) {
  if (dc->debugger) {
    debugger_tick(dc->debugger);
  }

  if (!dc->suspended) {
    scheduler_tick(dc->scheduler, ns);
  }
}

void dc_paint(dreamcast_t *dc, bool show_main_menu) {
  device_t *dev = dc->devices;

  while (dev) {
    if (dev->window && dev->window->paint) {
      dev->window->paint(dev, show_main_menu);
    }

    dev = dev->next;
  }
}

void dc_keydown(dreamcast_t *dc, keycode_t code, int16_t value) {
  device_t *dev = dc->devices;

  while (dev) {
    if (dev->window && dev->window->keydown) {
      dev->window->keydown(dev, code, value);
    }

    dev = dev->next;
  }
}
