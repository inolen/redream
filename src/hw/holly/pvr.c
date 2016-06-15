#include "hw/holly/holly.h"
#include "hw/holly/pvr.h"
#include "hw/holly/ta.h"
#include "hw/sh4/sh4.h"
#include "hw/dreamcast.h"

static void pvr_next_scanline(pvr_t *pvr) {
  uint32_t num_scanlines = pvr->SPG_LOAD->vcount + 1;
  if (pvr->current_scanline > num_scanlines) {
    pvr->current_scanline = 0;
  }

  // vblank in
  if (pvr->current_scanline == pvr->SPG_VBLANK_INT->vblank_in_line_number) {
    holly_raise_interrupt(pvr->holly, HOLLY_INTC_PCVIINT);
  }

  // vblank out
  if (pvr->current_scanline == pvr->SPG_VBLANK_INT->vblank_out_line_number) {
    holly_raise_interrupt(pvr->holly, HOLLY_INTC_PCVOINT);
  }

  // hblank in
  holly_raise_interrupt(pvr->holly, HOLLY_INTC_PCHIINT);

  // bool was_vsync = SPG_STATUS.vsync;
  pvr->SPG_STATUS->vsync =
      pvr->SPG_VBLANK->vbstart < pvr->SPG_VBLANK->vbend
          ? (pvr->current_scanline >= pvr->SPG_VBLANK->vbstart &&
             pvr->current_scanline < pvr->SPG_VBLANK->vbend)
          : (pvr->current_scanline >= pvr->SPG_VBLANK->vbstart ||
             pvr->current_scanline < pvr->SPG_VBLANK->vbend);
  pvr->SPG_STATUS->scanline = pvr->current_scanline++;

  // FIXME toggle SPG_STATUS.fieldnum on vblank?
  // if (!was_vsync && SPG_STATUS.vsync) {
  // }

  // reschedule
  pvr->line_timer =
      scheduler_start_timer(pvr->scheduler, (timer_cb)&pvr_next_scanline, pvr,
                            HZ_TO_NANO(pvr->line_clock));
}

static void pvr_reconfigure_spg(pvr_t *pvr) {
  // get and scale pixel clock frequency
  int pixel_clock = 13500000;
  if (pvr->FB_R_CTRL->vclk_div) {
    pixel_clock *= 2;
  }

  // hcount is number of pixel clock cycles per line - 1
  pvr->line_clock = pixel_clock / (pvr->SPG_LOAD->hcount + 1);
  if (pvr->SPG_CONTROL->interlace) {
    pvr->line_clock *= 2;
  }

  LOG_INFO(
      "ReconfigureSPG: pixel_clock %d, line_clock %d, vcount %d, hcount %d, "
      "interlace %d, vbstart %d, vbend %d",
      pixel_clock, pvr->line_clock, pvr->SPG_LOAD->vcount,
      pvr->SPG_LOAD->hcount, pvr->SPG_CONTROL->interlace,
      pvr->SPG_VBLANK->vbstart, pvr->SPG_VBLANK->vbend);

  if (pvr->line_timer) {
    scheduler_cancel_timer(pvr->scheduler, pvr->line_timer);
    pvr->line_timer = NULL;
  }

  pvr->line_timer =
      scheduler_start_timer(pvr->scheduler, (timer_cb)&pvr_next_scanline, pvr,
                            HZ_TO_NANO(pvr->line_clock));
}

static uint32_t pvr_reg_r32(pvr_t *pvr, uint32_t addr) {
  uint32_t offset = addr >> 2;
  reg_read_cb read = pvr->reg_read[offset];

  if (read) {
    void *data = pvr->reg_data[offset];
    return read(data);
  }

  return pvr->reg[offset];
}

static void pvr_reg_w32(pvr_t *pvr, uint32_t addr, uint32_t value) {
  uint32_t offset = addr >> 2;
  reg_write_cb write = pvr->reg_write[offset];

  // ID register is read-only, and the bios will fail to boot if a write
  // goes through to this register
  if (offset == ID) {
    return;
  }

  uint32_t old_value = pvr->reg[offset];
  pvr->reg[offset] = (uint32_t)value;

  if (write) {
    void *data = pvr->reg_data[offset];
    write(data, old_value, &pvr->reg[offset]);
  }
}

static uint32_t MAP64(uint32_t addr) {
  // the dreamcast has 8MB of vram, split into two 4MB banks, with two ways of
  // accessing it:
  // 0x04000000 -> 0x047fffff, 32-bit sequential access
  // 0x05000000 -> 0x057fffff, 64-bit interleaved access
  //
  // in 64-bit interleaved mode, the addresses map like so:
  // 0x05000000 = 0x0400000
  // 0x05400000 = 0x0400004
  // 0x05400002 = 0x0400006
  // 0x05000004 = 0x0400008
  // 0x05000006 = 0x040000a
  // 0x05400004 = 0x040000c
  // 0x05000008 = 0x0400010
  // 0x05400008 = 0x0400014
  // 0x0500000c = 0x0400018
  // 0x0540000c = 0x040001c
  return (((addr & 0x003ffffc) << 1) + ((addr & 0x00400000) >> 20) +
          (addr & 0x3));
}

#define define_vram_interleaved_read(name, type)                       \
  static type pvr_vram_interleaved_##name(pvr_t *pvr, uint32_t addr) { \
    addr = MAP64(addr);                                                \
    return *(type *)&pvr->video_ram[addr];                             \
  }

define_vram_interleaved_read(r8, uint8_t);
define_vram_interleaved_read(r16, uint16_t);
define_vram_interleaved_read(r32, uint32_t);

#define define_vram_interleaved_write(name, type)                    \
  static void pvr_vram_interleaved_##name(pvr_t *pvr, uint32_t addr, \
                                          type value) {              \
    addr = MAP64(addr);                                              \
    *(type *)&pvr->video_ram[addr] = value;                          \
  }

define_vram_interleaved_write(w8, uint8_t);
define_vram_interleaved_write(w16, uint16_t);
define_vram_interleaved_write(w32, uint32_t);

REG_W32(pvr_t *pvr, SPG_LOAD) {
  pvr_reconfigure_spg(pvr);
}

REG_W32(pvr_t *pvr, FB_R_CTRL) {
  pvr_reconfigure_spg(pvr);
}

static bool pvr_init(pvr_t *pvr) {
  dreamcast_t *dc = pvr->base.dc;

  pvr->scheduler = dc->scheduler;
  pvr->holly = dc->holly;
  pvr->space = dc->sh4->base.memory->space;
  pvr->palette_ram = as_translate(pvr->space, 0x005f9000);
  pvr->video_ram = as_translate(pvr->space, 0x04000000);

#define PVR_REG_R32(name)    \
  pvr->reg_data[name] = pvr; \
  pvr->reg_read[name] = (reg_read_cb)&name##_r;
#define PVR_REG_W32(name)    \
  pvr->reg_data[name] = pvr; \
  pvr->reg_write[name] = (reg_write_cb)&name##_w;
#define PVR_REG(offset, name, default, type) \
  pvr->reg[name] = default;                  \
  pvr->name = (type *)&pvr->reg[name];

  PVR_REG_W32(SPG_LOAD);
  PVR_REG_W32(FB_R_CTRL);
  #include "hw/holly/pvr_regs.inc"

#undef PVR_REG_R32
#undef PVR_REG_W32
#undef PVR_REG

  // configure initial vsync interval
  pvr_reconfigure_spg(pvr);

  return true;
}

pvr_t *pvr_create(dreamcast_t *dc) {
  pvr_t *pvr =
      dc_create_device(dc, sizeof(pvr_t), "pvr", (device_init_cb)&pvr_init);

  return pvr;
}

void pvr_destroy(pvr_t *pvr) {
  dc_destroy_device(&pvr->base);
}

// clang-format off
AM_BEGIN(pvr_t, pvr_reg_map);
  AM_RANGE(0x00000000, 0x00000fff) AM_HANDLE(NULL,
                                             NULL,
                                             (r32_cb)&pvr_reg_r32,
                                             NULL,
                                             NULL,
                                             NULL,
                                             (w32_cb)&pvr_reg_w32,
                                             NULL)
  AM_RANGE(0x00001000, 0x00001fff) AM_MOUNT()
AM_END()

AM_BEGIN(pvr_t, pvr_vram_map);
  AM_RANGE(0x00000000, 0x007fffff) AM_MOUNT()
  AM_RANGE(0x01000000, 0x017fffff) AM_HANDLE((r8_cb)&pvr_vram_interleaved_r8,
                                             (r16_cb)&pvr_vram_interleaved_r16,
                                             (r32_cb)&pvr_vram_interleaved_r32,
                                             NULL,
                                             (w8_cb)&pvr_vram_interleaved_w8,
                                             (w16_cb)&pvr_vram_interleaved_w16,
                                             (w32_cb)&pvr_vram_interleaved_w32,
                                             NULL)
AM_END();
// clang-format on
