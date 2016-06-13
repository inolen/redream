#ifndef DREAMCAST_H
#define DREAMCAST_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "core/list.h"
#include "hw/memory.h"
#include "ui/keycode.h"

#ifdef __cplusplus
extern "C" {
#endif

struct address_map_s;
struct aica_s;
struct arm_s;
struct debugger_s;
struct device_s;
struct dreamcast_s;
struct gdrom_s;
struct holly_s;
struct maple_s;
struct memory_s;
struct pvr_s;
struct scheduler_s;
struct sh4_s;
struct ta_s;

//
// register access helpers
//
#define DECLARE_REG_R32(self, name) uint32_t name##_r(self)
#define DECLARE_REG_W32(self, name) \
  static void name##_w(self, uint32_t, uint32_t *)

#define REG_R32(self, name) uint32_t name##_r(self)
#define REG_W32(self, name) \
  static void name##_w(self, uint32_t old_value, uint32_t *new_value)

typedef uint32_t (*reg_read_cb)(void *);
typedef void (*reg_write_cb)(void *, uint32_t, uint32_t *);

//
// device interfaces
//
typedef int (*device_num_regs_cb)(struct device_s *);
typedef void (*device_step_cb)(struct device_s *);
typedef void (*device_add_bp_cb)(struct device_s *, int, uint32_t);
typedef void (*device_rem_bp_cb)(struct device_s *, int, uint32_t);
typedef void (*device_read_mem_cb)(struct device_s *, uint32_t, uint8_t *, int);
typedef void (*device_read_reg_cb)(struct device_s *, int, uint64_t *, int *);

typedef struct debug_interface_s {
  device_num_regs_cb num_regs;
  device_step_cb step;
  device_add_bp_cb add_bp;
  device_rem_bp_cb rem_bp;
  device_read_mem_cb read_mem;
  device_read_reg_cb read_reg;
} debug_interface_t;

typedef void (*device_set_pc_cb)(struct device_s *, uint32_t);
typedef void (*device_run_cb)(struct device_s *, int64_t);

typedef struct {
  device_run_cb run;
  bool suspended;
} execute_interface_t;

execute_interface_t *execute_interface_create(device_run_cb run);
void execute_interface_destroy(execute_interface_t *execute);

typedef struct {
  address_map_cb mapper;
  struct address_space_s *space;
} memory_interface_t;

memory_interface_t *memory_interface_create(struct dreamcast_s *dc,
                                            address_map_cb mapper);
void memory_interface_destroy(memory_interface_t *memory);

typedef void (*device_paint_cb)(struct device_s *, bool);
typedef void (*device_keydown_cb)(struct device_s *, keycode_t, int16_t);

typedef struct {
  device_paint_cb paint;
  device_keydown_cb keydown;
} window_interface_t;

window_interface_t *window_interface_create(device_paint_cb paint,
                                            device_keydown_cb keydown);
void window_interface_destroy(window_interface_t *window);

//
// device
//
typedef bool (*device_init_cb)(struct device_s *);

typedef struct device_s {
  struct dreamcast_s *dc;
  const char *name;
  device_init_cb init;
  debug_interface_t *debug;
  execute_interface_t *execute;
  memory_interface_t *memory;
  window_interface_t *window;
  list_node_t it;
} device_t;

//
// machine
//
typedef struct dreamcast_s {
  struct debugger_s *debugger;
  struct memory_s *memory;
  struct scheduler_s *scheduler;
  struct sh4_s *sh4;
  struct arm_s *arm;
  struct aica_s *aica;
  struct holly_s *holly;
  struct gdrom_s *gdrom;
  struct maple_s *maple;
  struct pvr_s *pvr;
  struct ta_s *ta;
  bool suspended;
  list_t devices;
} dreamcast_t;

void *dc_create_device(dreamcast_t *dc, size_t size, const char *name,
                       device_init_cb init);
device_t *dc_get_device(dreamcast_t *dc, const char *name);
void dc_destroy_device(device_t *dev);

bool dc_init(dreamcast_t *dc);
void dc_suspend(dreamcast_t *dc);
void dc_resume(dreamcast_t *dc);
void dc_tick(dreamcast_t *dc, int64_t ns);
void dc_paint(dreamcast_t *dc, bool show_main_menu);
void dc_keydown(dreamcast_t *dc, keycode_t code, int16_t value);

dreamcast_t *dc_create(void *rb);
void dc_destroy(dreamcast_t *dc);

#ifdef __cplusplus
}
#endif

#endif
