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

DEFINE_OPTION_INT(gdb, 0, "Run gdb debug server");

void dc_finish_render(struct dreamcast *dc) {
  if (dc->client.finish_render) {
    dc->client.finish_render(dc->client.userdata);
  }
}

void dc_start_render(struct dreamcast *dc, struct tile_ctx *ctx) {
  if (dc->client.start_render) {
    dc->client.start_render(dc->client.userdata, ctx);
  }
}

void dc_joy_remove(struct dreamcast *dc, int joystick_index) {
  list_for_each_entry(dev, &dc->devices, struct device, it) {
    if (dev->window_if && dev->window_if->joy_remove) {
      dev->window_if->joy_remove(dev, joystick_index);
    }
  }
}

void dc_joy_add(struct dreamcast *dc, int joystick_index) {
  list_for_each_entry(dev, &dc->devices, struct device, it) {
    if (dev->window_if && dev->window_if->joy_add) {
      dev->window_if->joy_add(dev, joystick_index);
    }
  }
}

void dc_keydown(struct dreamcast *dc, int device_index, enum keycode code,
                int16_t value) {
  list_for_each_entry(dev, &dc->devices, struct device, it) {
    if (dev->window_if && dev->window_if->keydown) {
      dev->window_if->keydown(dev, device_index, code, value);
    }
  }
}

void dc_debug_menu(struct dreamcast *dc, struct nk_context *ctx) {
  list_for_each_entry(dev, &dc->devices, struct device, it) {
    if (dev->debug_menu) {
      dev->debug_menu(dev, ctx);
    }
  }
}

void dc_tick(struct dreamcast *dc, int64_t ns) {
  if (dc->debugger) {
    debugger_tick(dc->debugger);
  }

  if (dc->running) {
    scheduler_tick(dc->scheduler, ns);
  }
}

void dc_resume(struct dreamcast *dc) {
  dc->running = 1;
}

void dc_suspend(struct dreamcast *dc) {
  dc->running = 0;
}

int dc_init(struct dreamcast *dc) {
  if (dc->debugger && !debugger_init(dc->debugger)) {
    return 0;
  }

  if (!memory_init(dc->memory)) {
    return 0;
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
      return 0;
    }
  }

  return 1;
}

void dc_destroy_window_interface(struct window_interface *window) {
  free(window);
}

struct window_interface *dc_create_window_interface(
    device_keydown_cb keydown, device_joy_add_cb joy_add,
    device_joy_remove_cb joy_remove) {
  struct window_interface *window = calloc(1, sizeof(struct window_interface));
  window->keydown = keydown;
  window->joy_add = joy_add;
  window->joy_remove = joy_remove;
  return window;
}

void dc_destroy_memory_interface(struct memory_interface *memory) {
  as_destroy(memory->space);
  free(memory);
}

struct memory_interface *dc_create_memory_interface(struct dreamcast *dc,
                                                    address_map_cb mapper) {
  struct memory_interface *memory = calloc(1, sizeof(struct memory_interface));
  memory->mapper = mapper;
  memory->space = as_create(dc);
  return memory;
}

void dc_destroy_execute_interface(struct execute_interface *execute) {
  free(execute);
}

struct execute_interface *dc_create_execute_interface(device_run_cb run,
                                                      int running) {
  struct execute_interface *execute =
      calloc(1, sizeof(struct execute_interface));
  execute->run = run;
  execute->running = running;
  return execute;
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
                       int (*init)(struct device *),
                       void (*debug_menu)(struct device *,
                                          struct nk_context *)) {
  struct device *dev = calloc(1, size);

  dev->dc = dc;
  dev->name = name;
  dev->init = init;
  dev->debug_menu = debug_menu;

  list_add(&dc->devices, &dev->it);

  return dev;
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

struct dreamcast *dc_create(const struct dreamcast_client *client) {
  struct dreamcast *dc = calloc(1, sizeof(struct dreamcast));

  if (client) {
    dc->client = *client;
  }

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
  dc->ta = ta_create(dc);

  int res = dc_init(dc);
  CHECK(res);

  return dc;
}
