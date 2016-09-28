#include "hw/aica/aica.h"
#include "core/log.h"
#include "hw/aica/aica_types.h"
#include "hw/arm/arm.h"
#include "hw/dreamcast.h"
#include "hw/memory.h"
#include "hw/sh4/sh4.h"

struct aica {
  struct device;

  struct arm *arm;
  uint8_t *aica_regs;
  uint8_t *wave_ram;
  struct common_data *common_data;
};

static void aica_update_arm(struct aica *aica) {}

static void aica_update_sh(struct aica *aica) {}

#define define_reg_read(name, type)                        \
  type aica_reg_##name(struct aica *aica, uint32_t addr) { \
    return *(type *)&aica->aica_regs[addr];                \
  }

define_reg_read(r8, uint8_t);
define_reg_read(r16, uint16_t);
define_reg_read(r32, uint32_t);

#define define_reg_write(name, type)                                   \
  void aica_reg_##name(struct aica *aica, uint32_t addr, type value) { \
    *(type *)&aica->aica_regs[addr] = value;                           \
                                                                       \
    switch (addr) {                                                    \
      case 0x2c00: { /* ARMRST */                                      \
        if (value) {                                                   \
          arm_suspend(aica->arm);                                      \
        } else {                                                       \
          arm_resume(aica->arm);                                       \
        }                                                              \
      } break;                                                         \
    }                                                                  \
  }

define_reg_write(w8, uint8_t);
define_reg_write(w16, uint16_t);
define_reg_write(w32, uint32_t);

#define define_read_wave(name, type)                        \
  type aica_wave_##name(struct aica *aica, uint32_t addr) { \
    return *(type *)&aica->wave_ram[addr];                  \
  }

define_read_wave(r8, uint8_t);
define_read_wave(r16, uint16_t);
uint32_t aica_wave_r32(struct aica *aica, uint32_t addr) {
  // FIXME temp hacks to get Crazy Taxi 1 booting
  if (addr == 0x104 || addr == 0x284 || addr == 0x288) {
    return 0x54494e49;
  }
  // FIXME temp hacks to get Crazy Taxi 2 booting
  if (addr == 0x5c) {
    return 0x54494e49;
  }
  // FIXME temp hacks to get PoP booting
  if (addr == 0xb200 || addr == 0xb210 || addr == 0xb220 || addr == 0xb230 ||
      addr == 0xb240 || addr == 0xb250 || addr == 0xb260 || addr == 0xb270 ||
      addr == 0xb280 || addr == 0xb290 || addr == 0xb2a0 || addr == 0xb2b0 ||
      addr == 0xb2c0 || addr == 0xb2d0 || addr == 0xb2e0 || addr == 0xb2f0 ||
      addr == 0xb300 || addr == 0xb310 || addr == 0xb320 || addr == 0xb330 ||
      addr == 0xb340 || addr == 0xb350 || addr == 0xb360 || addr == 0xb370 ||
      addr == 0xb380 || addr == 0xb390 || addr == 0xb3a0 || addr == 0xb3b0 ||
      addr == 0xb3c0 || addr == 0xb3d0 || addr == 0xb3e0 || addr == 0xb3f0) {
    return 0x0;
  }

  return *(uint32_t *)&aica->wave_ram[addr];
}

#define define_write_wave(name, type)                                   \
  void aica_wave_##name(struct aica *aica, uint32_t addr, type value) { \
    *(type *)&aica->wave_ram[addr] = value;                             \
  }

define_write_wave(w8, uint8_t);
define_write_wave(w16, uint16_t);
define_write_wave(w32, uint32_t);

static bool aica_init(struct device *dev) {
  struct aica *aica = (struct aica *)dev;
  struct dreamcast *dc = aica->dc;

  aica->arm = dc->arm;
  aica->aica_regs = memory_translate(dc->memory, "aica reg ram", 0x00000000);
  aica->wave_ram = memory_translate(dc->memory, "aica wave ram", 0x00000000);
  aica->common_data = (struct common_data *)(aica->aica_regs + 0x2800);

  arm_suspend(aica->arm);

  return true;
}

struct aica *aica_create(struct dreamcast *dc) {
  struct aica *aica =
      dc_create_device(dc, sizeof(struct aica), "aica", &aica_init);
  return aica;
}

void aica_destroy(struct aica *aica) {
  dc_destroy_device((struct device *)aica);
}

// clang-format off
AM_BEGIN(struct aica, aica_reg_map);
  AM_RANGE(0x00000000, 0x00010fff) AM_MOUNT("aica reg ram")
  AM_RANGE(0x00000000, 0x00010fff) AM_HANDLE("aica reg",
                                             (r8_cb)&aica_reg_r8,
                                             (r16_cb)&aica_reg_r16,
                                             (r32_cb)&aica_reg_r32,
                                             NULL,
                                             (w8_cb)&aica_reg_w8,
                                             (w16_cb)&aica_reg_w16,
                                             (w32_cb)&aica_reg_w32,
                                             NULL)
AM_END();

AM_BEGIN(struct aica, aica_data_map);
  AM_RANGE(0x00000000, 0x007fffff) AM_MOUNT("aica wave ram")
  AM_RANGE(0x00000000, 0x007fffff) AM_HANDLE("aica wave",
                                             (r8_cb)&aica_wave_r8,
                                             (r16_cb)&aica_wave_r16,
                                             (r32_cb)&aica_wave_r32,
                                             NULL,
                                             (w8_cb)&aica_wave_w8,
                                             (w16_cb)&aica_wave_w16,
                                             (w32_cb)&aica_wave_w32,
                                             NULL)
AM_END();
// clang-format on
