#ifndef DREAMCAST_H
#define DREAMCAST_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "core/list.h"
#include "hw/memory.h"
#include "ui/keycode.h"

struct aica;
struct arm;
struct debugger;
struct device;
struct dreamcast;
struct gdrom;
struct holly;
struct maple;
struct memory;
struct nk_context;
struct pvr;
struct rb;
struct scheduler;
struct sh4;
struct ta;

//
// register access helpers
//

#define REG_R32(self, name) uint32_t name##_r(self)
#define REG_W32(self, name) \
  static void name##_w(self, uint32_t old_value, uint32_t *new_value)

typedef uint32_t (*reg_read_cb)(void *);
typedef void (*reg_write_cb)(void *, uint32_t, uint32_t *);

//
// device interfaces
//

// debug interface
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

// execute interface
typedef void (*device_run_cb)(struct device *, int64_t);

struct execute_interface {
  device_run_cb run;
  bool suspended;
};

struct execute_interface *execute_interface_create(device_run_cb run);
void execute_interface_destroy(struct execute_interface *execute);

// memory interface
struct memory_interface {
  address_map_cb mapper;
  struct address_space *space;
};

struct memory_interface *memory_interface_create(struct dreamcast *dc,
                                                 address_map_cb mapper);
void memory_interface_destroy(struct memory_interface *memory);

typedef void (*device_paint_cb)(struct device *);
typedef void (*device_paint_debug_menu_cb)(struct device *,
                                           struct nk_context *);
typedef void (*device_keydown_cb)(struct device *, enum keycode, int16_t);

// winder interface
struct window_interface {
  device_paint_cb paint;
  device_paint_debug_menu_cb paint_debug_menu;
  device_keydown_cb keydown;
};

struct window_interface *window_interface_create(
    device_paint_cb paint, device_paint_debug_menu_cb paint_debug_menu,
    device_keydown_cb keydown);
void window_interface_destroy(struct window_interface *window);

//
// device
//

struct device {
  struct dreamcast *dc;
  const char *name;
  bool (*init)(struct device *dev);
  struct debug_interface *debug;
  struct execute_interface *execute;
  struct memory_interface *memory;
  struct window_interface *window;
  struct list_node it;
};

//
// machine
//
struct dreamcast {
  struct debugger *debugger;
  struct memory *memory;
  struct scheduler *scheduler;
  struct sh4 *sh4;
  struct arm *arm;
  struct aica *aica;
  struct holly *holly;
  struct gdrom *gdrom;
  struct maple *maple;
  struct pvr *pvr;
  struct ta *ta;
  bool suspended;
  struct list devices;
};

void *dc_create_device(struct dreamcast *dc, size_t size, const char *name,
                       bool (*init)(struct device *dev));
struct device *dc_get_device(struct dreamcast *dc, const char *name);
void dc_destroy_device(struct device *dev);

bool dc_init(struct dreamcast *dc);
void dc_suspend(struct dreamcast *dc);
void dc_resume(struct dreamcast *dc);
void dc_tick(struct dreamcast *dc, int64_t ns);
void dc_paint(struct dreamcast *dc);
void dc_paint_debug_menu(struct dreamcast *dc, struct nk_context *ctx);
void dc_keydown(struct dreamcast *dc, enum keycode code, int16_t value);

struct dreamcast *dc_create(struct rb *rb);
void dc_destroy(struct dreamcast *dc);

#endif
