#include "hw/pvr/pvr.h"
#include "hw/dreamcast.h"
#include "hw/holly/holly.h"
#include "hw/pvr/ta.h"
#include "hw/scheduler.h"
#include "hw/sh4/sh4.h"
#include "sys/time.h"

DEFINE_AGGREGATE_COUNTER(pvr_vblanks);

struct reg_cb pvr_cb[NUM_PVR_REGS];

static void pvr_next_scanline(void *data) {
  struct pvr *pvr = data;

  uint32_t num_lines = pvr->SPG_LOAD->vcount + 1;
  if (pvr->current_line > num_lines) {
    pvr->current_line = 0;
  }

  /* vblank in */
  if (pvr->current_line == pvr->SPG_VBLANK_INT->vblank_in_line_number) {
    holly_raise_interrupt(pvr->holly, HOLLY_INTC_PCVIINT);
  }

  /* vblank out */
  if (pvr->current_line == pvr->SPG_VBLANK_INT->vblank_out_line_number) {
    holly_raise_interrupt(pvr->holly, HOLLY_INTC_PCVOINT);
  }

  /* hblank in */
  holly_raise_interrupt(pvr->holly, HOLLY_INTC_PCHIINT);

  int was_vsync = pvr->SPG_STATUS->vsync;
  if (pvr->SPG_VBLANK->vbstart < pvr->SPG_VBLANK->vbend) {
    pvr->SPG_STATUS->vsync = pvr->current_line >= pvr->SPG_VBLANK->vbstart &&
                             pvr->current_line < pvr->SPG_VBLANK->vbend;
  } else {
    pvr->SPG_STATUS->vsync = pvr->current_line >= pvr->SPG_VBLANK->vbstart ||
                             pvr->current_line < pvr->SPG_VBLANK->vbend;
  }
  pvr->SPG_STATUS->scanline = pvr->current_line++;

  /* FIXME toggle SPG_STATUS.fieldnum on vblank? */
  if (!was_vsync && pvr->SPG_STATUS->vsync) {
    prof_counter_add(COUNTER_pvr_vblanks, 1);
  }

  /* reschedule */
  pvr->line_timer = scheduler_start_timer(pvr->scheduler, &pvr_next_scanline,
                                          pvr, HZ_TO_NANO(pvr->line_clock));
}

static void pvr_reconfigure_spg(struct pvr *pvr) {
  /* scale pixel clock frequency */
  int pixel_clock = 13500000;
  if (pvr->FB_R_CTRL->vclk_div) {
    pixel_clock *= 2;
  }

  /* hcount is number of pixel clock cycles per line - 1 */
  pvr->line_clock = pixel_clock / (pvr->SPG_LOAD->hcount + 1);
  if (pvr->SPG_CONTROL->interlace) {
    pvr->line_clock *= 2;
  }

  const char *mode = "VGA";
  if (pvr->SPG_CONTROL->NTSC == 1) {
    mode = "NTSC";
  } else if (pvr->SPG_CONTROL->PAL == 1) {
    mode = "PAL";
  }

  LOG_INFO(
      "pvr_reconfigure_spg mode %s, pixel_clock %d, line_clock %d, vcount %d, "
      "hcount %d"
      ", interlace %d, vbstart %d, vbend %d",
      mode, pixel_clock, pvr->line_clock, pvr->SPG_LOAD->vcount,
      pvr->SPG_LOAD->hcount, pvr->SPG_CONTROL->interlace,
      pvr->SPG_VBLANK->vbstart, pvr->SPG_VBLANK->vbend);

  if (pvr->line_timer) {
    scheduler_cancel_timer(pvr->scheduler, pvr->line_timer);
    pvr->line_timer = NULL;
  }

  pvr->line_timer = scheduler_start_timer(pvr->scheduler, &pvr_next_scanline,
                                          pvr, HZ_TO_NANO(pvr->line_clock));
}

static uint32_t pvr_reg_read(struct pvr *pvr, uint32_t addr,
                             uint32_t data_mask) {
  uint32_t offset = addr >> 2;
  reg_read_cb read = pvr_cb[offset].read;

  if (read) {
    return read(pvr->dc);
  }

  return pvr->reg[offset];
}

static void pvr_reg_write(struct pvr *pvr, uint32_t addr, uint32_t data,
                          uint32_t data_mask) {
  uint32_t offset = addr >> 2;
  reg_write_cb write = pvr_cb[offset].write;

  /*
   * ID register is read-only, and the bios will fail to boot if a write
   * goes through to this register
   */
  if (offset == ID) {
    return;
  }

  if (write) {
    write(pvr->dc, data);
    return;
  }

  pvr->reg[offset] = data;
}

static uint32_t pvr_palette_read(struct pvr *pvr, uint32_t addr,
                                 uint32_t data_mask) {
  return READ_DATA(&pvr->palette_ram[addr]);
}

static void pvr_palette_write(struct pvr *pvr, uint32_t addr, uint32_t data,
                              uint32_t data_mask) {
  WRITE_DATA(&pvr->palette_ram[addr]);
}

static uint32_t MAP64(uint32_t addr) {
  /*
   * the dreamcast has 8MB of vram, split into two 4MB banks, with two ways of
   * accessing it:
   * 0x04000000 -> 0x047fffff, 32-bit sequential access
   * 0x05000000 -> 0x057fffff, 64-bit interleaved access
   *
   * in 64-bit interleaved mode, the addresses map like so:
   * 0x05000000 = 0x0400000
   * 0x05400000 = 0x0400004
   * 0x05400002 = 0x0400006
   * 0x05000004 = 0x0400008
   * 0x05000006 = 0x040000a
   * 0x05400004 = 0x040000c
   * 0x05000008 = 0x0400010
   * 0x05400008 = 0x0400014
   * 0x0500000c = 0x0400018
   * 0x0540000c = 0x040001c
   */
  return (((addr & 0x003ffffc) << 1) + ((addr & 0x00400000) >> 20) +
          (addr & 0x3));
}

static uint32_t pvr_vram_interleaved_read(struct pvr *pvr, uint32_t addr,
                                          uint32_t data_mask) {
  addr = MAP64(addr);
  return READ_DATA(&pvr->rb_ram[addr]);
}

static void pvr_vram_interleaved_write(struct pvr *pvr, uint32_t addr,
                                       uint32_t data, uint32_t data_mask) {
  addr = MAP64(addr);
  WRITE_DATA(&pvr->rb_ram[addr]);
}

static bool pvr_init(struct device *dev) {
  struct pvr *pvr = (struct pvr *)dev;
  struct dreamcast *dc = pvr->dc;

/* init registers */
#define PVR_REG(offset, name, default, type) \
  pvr->reg[name] = default;                  \
  pvr->name = (type *)&pvr->reg[name];
#include "hw/pvr/pvr_regs.inc"
#undef PVR_REG

  pvr->palette_ram = (uint8_t *)pvr->PALETTE_RAM000;
  pvr->rb_ram = memory_translate(dc->memory, "video ram", 0x00000000);

  /* configure initial vsync interval */
  pvr_reconfigure_spg(pvr);

  return true;
}

void pvr_destroy(struct pvr *pvr) {
  dc_destroy_device((struct device *)pvr);
}

struct pvr *pvr_create(struct dreamcast *dc) {
  struct pvr *pvr = dc_create_device(dc, sizeof(struct pvr), "pvr", &pvr_init);

  return pvr;
}

REG_W32(pvr_cb, SPG_LOAD) {
  struct pvr *pvr = dc->pvr;
  pvr->SPG_LOAD->full = value;
  pvr_reconfigure_spg(pvr);
}

REG_W32(pvr_cb, FB_R_CTRL) {
  struct pvr *pvr = dc->pvr;
  pvr->FB_R_CTRL->full = value;
  pvr_reconfigure_spg(pvr);
}

/* clang-format off */
AM_BEGIN(struct pvr, pvr_reg_map);
  AM_RANGE(0x00000000, 0x00000fff) AM_HANDLE("pvr reg",
                                             (mmio_read_cb)&pvr_reg_read,
                                             (mmio_write_cb)&pvr_reg_write)
  AM_RANGE(0x00001000, 0x00001fff) AM_HANDLE("pvr palette",
                                             (mmio_read_cb)&pvr_palette_read,
                                             (mmio_write_cb)&pvr_palette_write)
AM_END();

AM_BEGIN(struct pvr, pvr_vram_map);
  AM_RANGE(0x00000000, 0x007fffff) AM_MOUNT("video ram")
  AM_RANGE(0x01000000, 0x017fffff) AM_HANDLE("video ram interleaved",
                                             (mmio_read_cb)&pvr_vram_interleaved_read,
                                             (mmio_write_cb)&pvr_vram_interleaved_write)
AM_END();
/* clang-format on */
