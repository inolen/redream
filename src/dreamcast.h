#ifndef DREAMCAST_H
#define DREAMCAST_H

#include <stddef.h>
#include <stdint.h>
#include "core/list.h"
#include "host/keycode.h"
#include "memory.h"

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
struct tile_context;

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

struct debug_interface {
  device_num_regs_cb num_regs;
  device_step_cb step;
  device_add_bp_cb add_bp;
  device_rem_bp_cb rem_bp;
  device_read_mem_cb read_mem;
  device_read_reg_cb read_reg;
};

/* execute interface */
typedef void (*device_run_cb)(struct device *, int64_t);

struct execute_interface {
  device_run_cb run;
  int running;
};

/* memory interface */
struct memory_interface {
  address_map_cb mapper;
  struct address_space *space;
};

/*
 * device
 */
typedef int (*device_init_cb)(struct device *);

struct device {
  struct dreamcast *dc;
  const char *name;
  device_init_cb init;

  /* optional interfaces */
  struct debug_interface *debug_if;
  struct execute_interface *execute_if;
  struct memory_interface *memory_if;

  /* cached references to other devices */
  struct debugger *debugger;
  struct memory *memory;
  struct scheduler *scheduler;
  struct bios *bios;
  struct sh4 *sh4;
  struct arm7 *arm;
  struct aica *aica;
  struct boot *boot;
  struct flash *flash;
  struct gdrom *gdrom;
  struct holly *holly;
  struct maple *maple;
  struct pvr *pvr;
  struct ta *ta;

  struct list_node it;
};

/*
 * machine
 */
typedef void (*push_audio_cb)(void *, const int16_t *, int);
typedef void (*start_render_cb)(void *, struct tile_context *);
typedef void (*finish_render_cb)(void *);
typedef void (*poll_input_cb)(void *);

struct dreamcast {
  int running;

  /* systems */
  struct debugger *debugger;
  struct memory *memory;
  struct scheduler *scheduler;

  /* devices */
  struct bios *bios;
  struct sh4 *sh4;
  struct arm7 *arm;
  struct aica *aica;
  struct boot *boot;
  struct flash *flash;
  struct gdrom *gdrom;
  struct holly *holly;
  struct maple *maple;
  struct pvr *pvr;
  struct ta *ta;
  struct list devices;

  /* client callbacks */
  void *userdata;
  push_audio_cb push_audio;
  start_render_cb start_render;
  finish_render_cb finish_render;
  poll_input_cb poll_input;
};

struct dreamcast *dc_create();
void dc_destroy(struct dreamcast *dc);

void *dc_create_device(struct dreamcast *dc, size_t size, const char *name,
                       device_init_cb init);
struct device *dc_get_device(struct dreamcast *dc, const char *name);
void dc_destroy_device(struct device *dev);

struct debug_interface *dc_create_debug_interface(device_num_regs_cb num_regs,
                                                  device_step_cb step,
                                                  device_add_bp_cb add_bp,
                                                  device_rem_bp_cb rem_bp,
                                                  device_read_mem_cb read_mem,
                                                  device_read_reg_cb read_reg);
void dc_destroy_debug_interface(struct debug_interface *dbg);

struct execute_interface *dc_create_execute_interface(device_run_cb run,
                                                      int running);
void dc_destroy_execute_interface(struct execute_interface *execute);

struct memory_interface *dc_create_memory_interface(struct dreamcast *dc,
                                                    address_map_cb mapper);
void dc_destroy_memory_interface(struct memory_interface *memory);

int dc_init(struct dreamcast *dc);
int dc_load(struct dreamcast *dc, const char *path);
void dc_suspend(struct dreamcast *dc);
void dc_resume(struct dreamcast *dc);
void dc_tick(struct dreamcast *dc, int64_t ns);
void dc_input(struct dreamcast *dc, int port, int button, int16_t value);

/* client functionality */
void dc_push_audio(struct dreamcast *dc, const int16_t *data, int frames);
void dc_start_render(struct dreamcast *dc, struct tile_context *ctx);
void dc_finish_render(struct dreamcast *dc);
void dc_poll_input(struct dreamcast *dc);

#endif
