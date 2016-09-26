#include "hw/arm/arm.h"
#include "hw/aica/aica.h"
#include "hw/dreamcast.h"

struct arm {
  struct device;
};

static bool arm_init(struct device *dev) {
  // struct arm *arm = container_of(dev, struct arm, base);
  return true;
}

static void arm_run(struct device *dev, int64_t ns) {
  // struct arm *arm = container_of(dev, struct arm, base);
}

void arm_suspend(struct arm *arm) {
  arm->execute->suspended = true;
}

void arm_resume(struct arm *arm) {
  arm->execute->suspended = false;
}

struct arm *arm_create(struct dreamcast *dc) {
  struct arm *arm = dc_create_device(dc, sizeof(struct arm), "arm", &arm_init);
  arm->execute = dc_create_execute_interface(&arm_run);
  return arm;
}

void arm_destroy(struct arm *arm) {
  dc_destroy_execute_interface(arm->execute);
  dc_destroy_device((struct device *)arm);
}

// clang-format off
AM_BEGIN(struct arm, arm_data_map);
  AM_RANGE(0x00000000, 0x007fffff) AM_MASK(0x00ffffff) AM_DEVICE("aica", aica_data_map)
  AM_RANGE(0x00800000, 0x00810fff) AM_MASK(0x00ffffff) AM_DEVICE("aica", aica_reg_map)
AM_END();
// clang-format on
