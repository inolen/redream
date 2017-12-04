#ifndef DREAMCAST_H
#define DREAMCAST_H

#include <stddef.h>
#include <stdint.h>
#include "core/constructor.h"
#include "core/list.h"
#include "host/keycode.h"

struct aica;
struct arm7;
struct bios;
struct boot;
struct debugger;
struct device;
struct dreamcast;
struct flash;
struct gdrom;
struct holly;
struct maple;
struct memory;
struct pvr;
struct scheduler;
struct sh4;
struct ta;
struct ta_context;

/*
 * register callbacks
*/
typedef uint32_t (*reg_read_cb)(struct dreamcast *);
typedef void (*reg_write_cb)(struct dreamcast *, uint32_t);

struct reg_cb {
  reg_read_cb read;
  reg_write_cb write;
};

#define REG_R32(callbacks, name)                     \
  static uint32_t name##_read(struct dreamcast *dc); \
  CONSTRUCTOR(REG_R32_INIT_##name) {                 \
    callbacks[name].read = &name##_read;             \
  }                                                  \
  uint32_t name##_read(struct dreamcast *dc)

#define REG_W32(callbacks, name)                                  \
  static void name##_write(struct dreamcast *dc, uint32_t value); \
  CONSTRUCTOR(REG_W32_INIT_##name) {                              \
    callbacks[name].write = &name##_write;                        \
  }                                                               \
  void name##_write(struct dreamcast *dc, uint32_t value)

/*
 * device interfaces
 */

/* debug interface */
typedef int (*device_num_regs_cb)(struct device *);
typedef void (*device_step_cb)(struct device *);
typedef void (*device_add_bp_cb)(struct device *, int, uint32_t);
typedef void (*device_rem_bp_cb)(struct device *, int, uint32_t);
typedef void (*device_read_mem_cb)(struct device *, uint32_t, uint8_t *, int);
typedef void (*device_read_reg_cb)(struct device *, int, uint64_t *, int *);

struct dbgif {
  int enabled;
  device_num_regs_cb num_regs;
  device_step_cb step;
  device_add_bp_cb add_bp;
  device_rem_bp_cb rem_bp;
  device_read_mem_cb read_mem;
  device_read_reg_cb read_reg;
};

/* run interface */
typedef void (*device_run_cb)(struct device *, int64_t);

struct runif {
  int enabled;
  int running;
  device_run_cb run;
};

/*
 * device
 */
typedef int (*device_init_cb)(struct device *);
typedef int (*device_post_init_cb)(struct device *);

struct device {
  struct dreamcast *dc;
  const char *name;

  /* called for each device during dc_init. at this point each device should
     initialize their own state, but not depend on the state of others */
  device_init_cb init;

  /* called for each device during dc_init, immediately after each device's
     init callback has been called. devices should perform initialization
     that depends on other device's state here */
  device_post_init_cb post_init;

  /* optional interfaces */
  struct dbgif dbgif;
  struct runif runif;

  struct list_node it;
};

/*
 * machine
 */
typedef void (*push_audio_cb)(void *, const int16_t *, int);
typedef void (*push_pixels_cb)(void *, const uint8_t *, int, int);
typedef void (*start_render_cb)(void *, struct ta_context *);
typedef void (*finish_render_cb)(void *);
typedef void (*vblank_in_cb)(void *, int);
typedef void (*vblank_out_cb)(void *);

struct dreamcast {
  int running;

  /* systems */
  struct debugger *debugger;
  struct memory *mem;
  struct scheduler *sched;

  /* devices */
  struct bios *bios;
  struct sh4 *sh4;
  struct arm7 *arm7;
  struct aica *aica;
  struct boot *boot;
  struct flash *flash;
  struct gdrom *gdrom;
  struct holly *holly;
  struct maple *maple;
  struct pvr *pvr;
  struct ta *ta;
  struct serial *serial;
  struct list devices;

  /* client callbacks */
  void *userdata;
  push_audio_cb push_audio;
  push_pixels_cb push_pixels;
  start_render_cb start_render;
  finish_render_cb finish_render;
  vblank_in_cb vblank_in;
  vblank_out_cb vblank_out;
};

struct dreamcast *dc_create();
void dc_destroy(struct dreamcast *dc);

int dc_init(struct dreamcast *dc);
int dc_load(struct dreamcast *dc, const char *path);
int dc_running(struct dreamcast *dc);
void dc_suspend(struct dreamcast *dc);
void dc_resume(struct dreamcast *dc);
void dc_tick(struct dreamcast *dc, int64_t ns);
void dc_input(struct dreamcast *dc, int port, int button, int16_t value);
void dc_add_serial_device(struct dreamcast *dc, struct serial *serial);
void dc_remove_serial_device(struct dreamcast *dc);

/* device registration */
void *dc_create_device(struct dreamcast *dc, size_t size, const char *name,
                       device_init_cb init, device_post_init_cb post_init);
struct device *dc_get_device(struct dreamcast *dc, const char *name);
void dc_destroy_device(struct device *dev);

/* client interface */
void dc_push_audio(struct dreamcast *dc, const int16_t *data, int frames);
void dc_push_pixels(struct dreamcast *dc, const uint8_t *data, int w, int h);
void dc_start_render(struct dreamcast *dc, struct ta_context *ctx);
void dc_finish_render(struct dreamcast *dc);
void dc_vblank_in(struct dreamcast *dc, int video_disabled);
void dc_vblank_out(struct dreamcast *dc);

#endif
