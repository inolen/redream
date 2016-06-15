#include "hw/aica/aica.h"
#include "hw/arm/arm.h"
#include "hw/dreamcast.h"

typedef struct arm_s { device_t base; } arm_t;

static bool arm_init(arm_t *arm) {
  return true;
}

static void arm_run(arm_t *arm, int64_t ns) {}

void arm_suspend(arm_t *arm) {
  arm->base.execute->suspended = true;
}

void arm_resume(arm_t *arm) {
  arm->base.execute->suspended = false;
}

arm_t *arm_create(dreamcast_t *dc) {
  arm_t *arm =
      dc_create_device(dc, sizeof(arm_t), "arm", (device_init_cb)&arm_init);
  arm->base.execute = execute_interface_create((device_run_cb)&arm_run);
  return arm;
}

void arm_destroy(arm_t *arm) {
  execute_interface_destroy(arm->base.execute);
  dc_destroy_device(&arm->base);
}

// clang-format off
AM_BEGIN(arm_t, arm_data_map);
  AM_RANGE(0x00000000, 0x007fffff) AM_MASK(0x00ffffff) AM_DEVICE("aica", aica_data_map)
  AM_RANGE(0x00800000, 0x00810fff) AM_MASK(0x00ffffff) AM_DEVICE("aica", aica_reg_map)
AM_END();
// clang-format on
