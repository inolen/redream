#include "dreamcast.h"
#include "bios/bios.h"
#include "core/option.h"
#include "core/string.h"
#include "debugger.h"
#include "hw/aica/aica.h"
#include "hw/arm7/arm7.h"
#include "hw/gdrom/gdrom.h"
#include "hw/holly/holly.h"
#include "hw/maple/maple.h"
#include "hw/pvr/pvr.h"
#include "hw/pvr/ta.h"
#include "hw/rom/boot.h"
#include "hw/rom/flash.h"
#include "hw/sh4/sh4.h"
#include "memory.h"
#include "scheduler.h"

void dc_poll_input(struct dreamcast *dc) {
  if (!dc->poll_input) {
    return;
  }

  dc->poll_input(dc->userdata);
}

void dc_finish_render(struct dreamcast *dc) {
  if (!dc->finish_render) {
    return;
  }

  dc->finish_render(dc->userdata);
}

void dc_start_render(struct dreamcast *dc, struct tile_context *ctx) {
  if (!dc->start_render) {
    return;
  }

  dc->start_render(dc->userdata, ctx);
}

void dc_push_audio(struct dreamcast *dc, const int16_t *data, int frames) {
  if (!dc->push_audio) {
    return;
  }

  dc->push_audio(dc->userdata, data, frames);
}

void dc_input(struct dreamcast *dc, int port, int button, int16_t value) {
  maple_handle_input(dc->maple, port, button, value);
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

static int dc_load_bin(struct dreamcast *dc, const char *path) {
  FILE *fp = fopen(path, "rb");
  if (!fp) {
    return 0;
  }

  fseek(fp, 0, SEEK_END);
  int size = ftell(fp);
  fseek(fp, 0, SEEK_SET);

  /* load to 0x0c010000 (area 3) which is where 1ST_READ.BIN is loaded to */
  uint8_t *data = memory_translate(dc->memory, "system ram", 0x00010000);
  int n = (int)fread(data, sizeof(uint8_t), size, fp);
  fclose(fp);

  if (n != size) {
    LOG_WARNING("failed to read %s", path);
    return 0;
  }

  sh4_reset(dc->sh4, 0x0c010000);
  dc_resume(dc);

  return 1;
}

static int dc_load_disc(struct dreamcast *dc, const char *path) {
  struct disc *disc = disc_create(path);

  if (!disc) {
    return 0;
  }

  gdrom_set_disc(dc->gdrom, disc);
  sh4_reset(dc->sh4, 0xa0000000);
  dc_resume(dc);

  return 1;
}

int dc_load(struct dreamcast *dc, const char *path) {
  if (!path) {
    /* boot to main menu of no path specified */
    sh4_reset(dc->sh4, 0xa0000000);
    dc_resume(dc);
    return 1;
  }

  LOG_INFO("loading %s", path);

  if (dc_load_disc(dc, path) || dc_load_bin(dc, path)) {
    return 1;
  }

  LOG_WARNING("failed to load %s", path);

  return 0;
}

int dc_init(struct dreamcast *dc) {
  if (dc->debugger && !debugger_init(dc->debugger)) {
    LOG_WARNING("failed to initialize debugger");
    return 0;
  }

  if (!memory_init(dc->memory)) {
    LOG_WARNING("failed to initialize shared memory");
    return 0;
  }

  /* cache references to other devices */
  list_for_each_entry(dev, &dc->devices, struct device, it) {
    dev->debugger = dc->debugger;
    dev->memory = dc->memory;
    dev->scheduler = dc->scheduler;
    dev->bios = dc->bios;
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
  }

  /* initialize each device */
  list_for_each_entry(dev, &dc->devices, struct device, it) {
    if (!dev->init(dev)) {
      LOG_WARNING("failed to initialize device '%s'", dev->name);
      return 0;
    }
  }

  if (!bios_init(dc->bios)) {
    LOG_WARNING("failed to initialize bios");
    return 0;
  }

  return 1;
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

void dc_destroy_debug_interface(struct debug_interface *dbg) {
  free(dbg);
}

struct debug_interface *dc_create_debug_interface(device_num_regs_cb num_regs,
                                                  device_step_cb step,
                                                  device_add_bp_cb add_bp,
                                                  device_rem_bp_cb rem_bp,
                                                  device_read_mem_cb read_mem,
                                                  device_read_reg_cb read_reg) {
  struct debug_interface *dbg = calloc(1, sizeof(struct debug_interface));
  dbg->num_regs = num_regs;
  dbg->step = step;
  dbg->add_bp = add_bp;
  dbg->rem_bp = rem_bp;
  dbg->read_mem = read_mem;
  dbg->read_reg = read_reg;
  return dbg;
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
                       device_init_cb init) {
  struct device *dev = calloc(1, size);

  dev->dc = dc;
  dev->name = name;
  dev->init = init;

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
  bios_destroy(dc->bios);
  scheduler_destroy(dc->scheduler);
  memory_destroy(dc->memory);
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
  dc->memory = memory_create(dc);
  dc->scheduler = scheduler_create(dc);
  dc->bios = bios_create(dc);
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
