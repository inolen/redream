#include "hw/dreamcast.h"

struct g2 {
  struct device;
};

static bool g2_init(struct device *dev) {
  return true;
}

struct g2 *g2_create(struct dreamcast *dc) {
  struct g2 *g2 = dc_create_device(dc, sizeof(struct g2), "g2", &g2_init);
  return g2;
}

void g2_destroy(struct g2 *g2) {
  dc_destroy_device((struct device *)g2);
}

// clang-format off
AM_BEGIN(struct g2, g2_modem_map);
  AM_RANGE(0x00000000, 0x0007ffff) AM_MOUNT("modem reg")
AM_END();

AM_BEGIN(struct g2, g2_expansion0_map);
  AM_RANGE(0x00000000, 0x00ffffff) AM_MOUNT("expansion 0")
AM_END();

AM_BEGIN(struct g2, g2_expansion1_map);
  AM_RANGE(0x00000000, 0x008fffff) AM_MOUNT("expansion 1")
AM_END();

AM_BEGIN(struct g2, g2_expansion2_map);
  AM_RANGE(0x00000000, 0x03ffffff) AM_MOUNT("expansion 2")
AM_END();
// clang-format on
