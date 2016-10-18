#include "hw/arm7/arm7.h"
#include "hw/aica/aica.h"
#include "hw/dreamcast.h"

struct arm7 {
  struct device;
};

static bool arm7_init(struct device *dev) {
  // struct arm7 *arm7 = (struct arm7 *)dev;
  return true;
}

static void arm7_run(struct device *dev, int64_t ns) {
  // struct arm *arm7 = (struct arm7 *)dev;
}

void arm7_suspend(struct arm7 *arm) {
  arm->execute_if->suspended = true;
}

void arm7_resume(struct arm7 *arm) {
  arm->execute_if->suspended = false;
}

struct arm7 *arm7_create(struct dreamcast *dc) {
  struct arm7 *arm = dc_create_device(dc, sizeof(struct arm7), "arm7", &arm7_init);
  arm->execute_if = dc_create_execute_interface(&arm7_run);
  arm->memory_if = dc_create_memory_interface(dc, &arm7_data_map);
  return arm;
}

void arm7_destroy(struct arm7 *arm) {
  dc_destroy_memory_interface(arm->memory_if);
  dc_destroy_execute_interface(arm->execute_if);
  dc_destroy_device((struct device *)arm);
}

// clang-format off
AM_BEGIN(struct arm7, arm7_data_map);
  AM_RANGE(0x00000000, 0x007fffff) AM_MASK(0x00ffffff) AM_DEVICE("aica", aica_data_map)
  AM_RANGE(0x00800000, 0x00810fff) AM_MASK(0x00ffffff) AM_DEVICE("aica", aica_reg_map)
AM_END();
// clang-format on
