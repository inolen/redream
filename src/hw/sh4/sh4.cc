#include <imgui.h>
#include "core/math.h"
#include "core/memory.h"
#include "core/profiler.h"
#include "hw/sh4/sh4.h"
#include "hw/sh4/sh4_code_cache.h"
#include "hw/dreamcast.h"
#include "hw/debugger.h"
#include "hw/memory.h"
#include "hw/scheduler.h"

#include "hw/aica/aica.h"
#include "hw/holly/holly.h"
#include "hw/holly/pvr.h"
#include "hw/holly/ta.h"

using namespace re::jit;
using namespace re::jit::backend;
using namespace re::jit::frontend::sh4;

static sh4_interrupt_info_t sh4_interrupts[NUM_SH_INTERRUPTS] = {
#define SH4_INT(name, intevt, pri, ipr, ipr_shift) \
  { intevt, pri, ipr, ipr_shift }                  \
  ,
#include "hw/sh4/sh4_int.inc"
#undef SH4_INT
};

static bool sh4_init(sh4_t *sh4);
static void sh4_paint(sh4_t *sh4, bool show_main_menu);
static uint32_t sh4_compile_pc();
static void sh4_invalid_instr(sh4_context_t *ctx, uint64_t data);
static void sh4_prefetch(sh4_context_t *ctx, uint64_t data);
static void sh4_sr_updated(sh4_context_t *ctx, uint64_t old_sr);
static void sh4_fpscr_updated(sh4_context_t *ctx, uint64_t old_fpscr);
static int sh4_compile_flags(sh4_t *sh4);
static void sh4_swap_gpr_bank(sh4_t *sh4);
static void sh4_swap_fpr_bank(sh4_t *sh4);
static void sh4_ccn_reset(sh4_t *sh4);
static void sh4_dmac_check(sh4_t *sh4, int channel);
static void sh4_intc_reprioritize(sh4_t *sh4);
static void sh4_intc_update_pending(sh4_t *sh4);
static void sh4_intc_check_pending(sh4_t *sh4);
static void sh4_tmu_update_tstr(sh4_t *sh4);
static void sh4_tmu_update_tcr(sh4_t *sh4, uint32_t n);
static void sh4_tmu_update_tcnt(sh4_t *sh4, uint32_t n);
static uint32_t sh4_tmu_tcnt(sh4_t *sh4, int n);
static void sh4_tmu_reschedule(sh4_t *sh4, int n, uint32_t tcnt, uint32_t tcr);
static void sh4_tmu_expire(sh4_t *sh4, int n);
static void sh4_tmu_expire_0(sh4_t *sh4);
static void sh4_tmu_expire_1(sh4_t *sh4);
static void sh4_tmu_expire_2(sh4_t *sh4);
template <typename T>
T sh4_read_reg(sh4_t *sh4, uint32_t addr);
template <typename T>
void sh4_write_reg(sh4_t *sh4, uint32_t addr, T value);
template <typename T>
T sh4_read_cache(sh4_t *sh4, uint32_t addr);
template <typename T>
void sh4_write_cache(sh4_t *sh4, uint32_t addr, T value);
template <typename T>
T sh4_read_sq(sh4_t *sh4, uint32_t addr);
template <typename T>
void sh4_write_sq(sh4_t *sh4, uint32_t addr, T value);
DECLARE_REG_R32(sh4_t *sh4, PDTRA);
DECLARE_REG_W32(sh4_t *sh4, MMUCR);
DECLARE_REG_W32(sh4_t *sh4, CCR);
DECLARE_REG_W32(sh4_t *sh4, CHCR0);
DECLARE_REG_W32(sh4_t *sh4, CHCR1);
DECLARE_REG_W32(sh4_t *sh4, CHCR2);
DECLARE_REG_W32(sh4_t *sh4, CHCR3);
DECLARE_REG_W32(sh4_t *sh4, DMAOR);
DECLARE_REG_W32(sh4_t *sh4, IPRA);
DECLARE_REG_W32(sh4_t *sh4, IPRB);
DECLARE_REG_W32(sh4_t *sh4, IPRC);
DECLARE_REG_W32(sh4_t *sh4, TSTR);
DECLARE_REG_W32(sh4_t *sh4, TCR0);
DECLARE_REG_W32(sh4_t *sh4, TCR1);
DECLARE_REG_W32(sh4_t *sh4, TCR2);
DECLARE_REG_R32(sh4_t *sh4, TCNT0);
DECLARE_REG_W32(sh4_t *sh4, TCNT0);
DECLARE_REG_R32(sh4_t *sh4, TCNT1);
DECLARE_REG_W32(sh4_t *sh4, TCNT1);
DECLARE_REG_R32(sh4_t *sh4, TCNT2);
DECLARE_REG_W32(sh4_t *sh4, TCNT2);

static sh4_t *g_sh4;

// clang-format off
AM_BEGIN(sh4_t, sh4_data_map)
  AM_RANGE(0x00000000, 0x03ffffff) AM_MOUNT()  // area 0
  AM_RANGE(0x04000000, 0x07ffffff) AM_MOUNT()  // area 1
  AM_RANGE(0x08000000, 0x0bffffff) AM_MOUNT()  // area 2
  AM_RANGE(0x0c000000, 0x0cffffff) AM_MOUNT()  // area 3
  AM_RANGE(0x10000000, 0x13ffffff) AM_MOUNT()  // area 4
  AM_RANGE(0x14000000, 0x17ffffff) AM_MOUNT()  // area 5
  AM_RANGE(0x18000000, 0x1bffffff) AM_MOUNT()  // area 6
  AM_RANGE(0x1c000000, 0x1fffffff) AM_MOUNT()  // area 7

  // main ram mirrors
  AM_RANGE(0x0d000000, 0x0dffffff) AM_MIRROR(0x0c000000)
  AM_RANGE(0x0e000000, 0x0effffff) AM_MIRROR(0x0c000000)
  AM_RANGE(0x0f000000, 0x0fffffff) AM_MIRROR(0x0c000000)

  // external devices
  AM_RANGE(0x005f6000, 0x005f7fff) AM_DEVICE("holly", holly_reg_map)
  AM_RANGE(0x005f8000, 0x005f9fff) AM_DEVICE("pvr", pvr_reg_map)
  AM_RANGE(0x00700000, 0x00710fff) AM_DEVICE("aica", aica_reg_map)
  AM_RANGE(0x00800000, 0x00ffffff) AM_DEVICE("aica", aica_data_map)
  AM_RANGE(0x04000000, 0x057fffff) AM_DEVICE("pvr", pvr_vram_map)
  AM_RANGE(0x10000000, 0x11ffffff) AM_DEVICE("ta", ta_fifo_map)

  // internal registers
  AM_RANGE(0x1e000000, 0x1fffffff) AM_HANDLE((r8_cb)&sh4_read_reg<uint8_t>,
                                             (r16_cb)&sh4_read_reg<uint16_t>,
                                             (r32_cb)&sh4_read_reg<uint32_t>,
                                             nullptr,
                                             (w8_cb)&sh4_write_reg<uint8_t>,
                                             (w16_cb)&sh4_write_reg<uint16_t>,
                                             (w32_cb)&sh4_write_reg<uint32_t>,
                                             nullptr)

  // physical mirrors
  AM_RANGE(0x20000000, 0x3fffffff) AM_MIRROR(0x00000000)  // p0
  AM_RANGE(0x40000000, 0x5fffffff) AM_MIRROR(0x00000000)  // p0
  AM_RANGE(0x60000000, 0x7fffffff) AM_MIRROR(0x00000000)  // p0
  AM_RANGE(0x80000000, 0x9fffffff) AM_MIRROR(0x00000000)  // p1
  AM_RANGE(0xa0000000, 0xbfffffff) AM_MIRROR(0x00000000)  // p2
  AM_RANGE(0xc0000000, 0xdfffffff) AM_MIRROR(0x00000000)  // p3
  AM_RANGE(0xe0000000, 0xffffffff) AM_MIRROR(0x00000000)  // p4

  // internal cache and sq only accessible through p4
  AM_RANGE(0x7c000000, 0x7fffffff) AM_HANDLE((r8_cb)&sh4_read_cache<uint8_t>,
                                             (r16_cb)&sh4_read_cache<uint16_t>,
                                             (r32_cb)&sh4_read_cache<uint32_t>,
                                             (r64_cb)&sh4_read_cache<uint64_t>,
                                             (w8_cb)&sh4_write_cache<uint8_t>,
                                             (w16_cb)&sh4_write_cache<uint16_t>,
                                             (w32_cb)&sh4_write_cache<uint32_t>,
                                             (w64_cb)&sh4_write_cache<uint64_t>)

  AM_RANGE(0xe0000000, 0xe3ffffff) AM_HANDLE((r8_cb)&sh4_read_sq<uint8_t>,
                                             (r16_cb)&sh4_read_sq<uint16_t>,
                                             (r32_cb)&sh4_read_sq<uint32_t>,
                                             nullptr,
                                             (w8_cb)&sh4_write_sq<uint8_t>,
                                             (w16_cb)&sh4_write_sq<uint16_t>,
                                             (w32_cb)&sh4_write_sq<uint32_t>,
                                             nullptr)
AM_END();
// clang-format on

static const int64_t SH4_CLOCK_FREQ = 200000000;

sh4_t *sh4_create(dreamcast_t *dc) {
  sh4_t *sh4 = reinterpret_cast<sh4_t *>(
      dc_create_device(dc, sizeof(sh4_t), "sh", (device_init_cb)&sh4_init));
  sh4->base.execute = execute_interface_create((device_run_cb)&sh4_run);
  sh4->base.memory = memory_interface_create(dc, sh4_data_map);
  sh4->base.window =
      window_interface_create((device_paint_cb)&sh4_paint, nullptr);

  g_sh4 = sh4;

  return sh4;
}

void sh4_destroy(sh4_t *sh4) {
  sh4_cache_destroy(sh4->code_cache);

  execute_interface_destroy(sh4->base.execute);
  memory_interface_destroy(sh4->base.memory);
  dc_destroy_device(&sh4->base);
}

bool sh4_init(sh4_t *sh4) {
  sh4->scheduler = sh4->base.dc->scheduler;
  sh4->space = sh4->base.memory->space;

  re::jit::backend::MemoryInterface memif = {
      &sh4->ctx,
      sh4->base.memory->space->protected_base,
      sh4->base.memory->space,
      &address_space_r8,
      &address_space_r16,
      &address_space_r32,
      &address_space_r64,
      &address_space_w8,
      &address_space_w16,
      &address_space_w32,
      &address_space_w64};
  sh4->code_cache = sh4_cache_create(&memif, &sh4_compile_pc);

  // initialize context
  sh4->ctx.sh4 = sh4;
  sh4->ctx.InvalidInstruction = &sh4_invalid_instr;
  sh4->ctx.Prefetch = &sh4_prefetch;
  sh4->ctx.SRUpdated = &sh4_sr_updated;
  sh4->ctx.FPSCRUpdated = &sh4_fpscr_updated;
  sh4->ctx.pc = 0xa0000000;
  sh4->ctx.r[15] = 0x8d000000;
  sh4->ctx.pr = 0x0;
  sh4->ctx.sr = 0x700000f0;
  sh4->ctx.fpscr = 0x00040001;

// initialize registers
#define SH4_REG_R32(name)    \
  sh4->reg_data[name] = sh4; \
  sh4->reg_read[name] = (reg_read_cb)&name##_r;
#define SH4_REG_W32(name)    \
  sh4->reg_data[name] = sh4; \
  sh4->reg_write[name] = (reg_write_cb)&name##_w;
  SH4_REG_R32(PDTRA);
  SH4_REG_W32(MMUCR);
  SH4_REG_W32(CCR);
  SH4_REG_W32(CHCR0);
  SH4_REG_W32(CHCR1);
  SH4_REG_W32(CHCR2);
  SH4_REG_W32(CHCR3);
  SH4_REG_W32(DMAOR);
  SH4_REG_W32(IPRA);
  SH4_REG_W32(IPRB);
  SH4_REG_W32(IPRC);
  SH4_REG_W32(TSTR);
  SH4_REG_W32(TCR0);
  SH4_REG_W32(TCR1);
  SH4_REG_W32(TCR2);
  SH4_REG_R32(TCNT0);
  SH4_REG_W32(TCNT0);
  SH4_REG_R32(TCNT1);
  SH4_REG_W32(TCNT1);
  SH4_REG_R32(TCNT2);
  SH4_REG_W32(TCNT2);
#undef SH4_REG_R32
#undef SH4_REG_W32

#define SH4_REG(addr, name, default, type) \
  sh4->reg[name] = default;                \
  sh4->name = reinterpret_cast<type *>(&sh4->reg[name]);
#include "hw/sh4/sh4_regs.inc"
#undef SH4_REG

  // reset interrupts
  sh4_intc_reprioritize(sh4);

  return true;
}

void sh4_set_pc(sh4_t *sh4, uint32_t pc) {
  sh4->ctx.pc = pc;
}

static void sh4_run_inner(sh4_t *sh4, int64_t ns) {
  // execute at least 1 cycle. the tests rely on this to step block by block
  int64_t cycles = std::max(NANO_TO_CYCLES(ns, SH4_CLOCK_FREQ), INT64_C(1));

  // each block's epilog will decrement the remaining cycles as they run
  sh4->ctx.num_cycles = static_cast<int>(cycles);

  while (sh4->ctx.num_cycles > 0) {
    code_pointer_t code = sh4_cache_get_code(sh4->code_cache, sh4->ctx.pc);
    sh4->ctx.pc = code();

    sh4_intc_check_pending(sh4);
  }

  // // track mips
  // auto now = std::chrono::high_resolution_clock::now();
  // auto next_time = last_mips_time_ + std::chrono::seconds(1);

  // if (now > next_time) {
  //   auto delta = std::chrono::duration_cast<std::chrono::nanoseconds>(
  //       now - last_mips_time_);
  //   auto delta_f =
  //       std::chrono::duration_cast<std::chrono::duration<float>>(delta).count();
  //   auto delta_scaled = delta_f * 1000000.0f;
  //   mips_[num_mips_++ % MAX_MIPS_SAMPLES] = sh4->ctx.num_instrs /
  //   delta_scaled;
  //   sh4->ctx.num_instrs = 0;
  //   last_mips_time_ = now;
  // }
}

void sh4_run(sh4_t *sh4, int64_t ns) {
  prof_enter("sh4_run");

  sh4_run_inner(sh4, ns);

  prof_leave();
}

// int SH4::NumRegisters() {
//   return 59;
// }

// void SH4::Step() {
//   // invalidate the block for the current pc
//   sh4->code_cache->RemoveBlocks(sh4->ctx.pc);

//   // recompile it with only one instruction and run it
//   uint32_t guest_addr = sh4->ctx.pc;
//   uint8_t *host_addr = space->Translate(guest_addr);
//   int flags = GetCompileFlags() | SH4_SINGLE_INSTR;

//   code_pointer_t code = sh4->code_cache->CompileCode(guest_addr, host_addr,
//   flags);
//   sh4->ctx.pc = code();

//   // let the debugger know we've stopped
//   dc_.base->debugger->Trap();
// }

// void SH4::AddBreakpoint(int type, uint32_t addr) {
//   // save off the original instruction
//   uint16_t instr = space->R16(addr);
//   breakpoints_.insert(std::make_pair(addr, instr));

//   // write out an invalid instruction
//   space->W16(addr, 0);

//   sh4->code_cache->RemoveBlocks(addr);
// }

// void SH4::RemoveBreakpoint(int type, uint32_t addr) {
//   // recover the original instruction
//   auto it = breakpoints_.find(addr);
//   CHECK_NE(it, breakpoints_.end());
//   uint16_t instr = it->second;
//   breakpoints_.erase(it);

//   // overwrite the invalid instruction with the original
//   space->W16(addr, instr);

//   sh4->code_cache->RemoveBlocks(addr);
// }

// void SH4::ReadMemory(uint32_t addr, uint8_t *buffer, int size) {
//   space->Memcpy(buffer, addr, size);
// }

// void SH4::ReadRegister(int n, uint64_t *value, int *size) {
//   if (n < 16) {
//     *value = sh4->ctx.r[n];
//   } else if (n == 16) {
//     *value = sh4->ctx.pc;
//   } else if (n == 17) {
//     *value = sh4->ctx.pr;
//   } else if (n == 18) {
//     *value = sh4->ctx.gbr;
//   } else if (n == 19) {
//     *value = sh4->ctx.vbr;
//   } else if (n == 20) {
//     *value = sh4->ctx.mach;
//   } else if (n == 21) {
//     *value = sh4->ctx.macl;
//   } else if (n == 22) {
//     *value = sh4->ctx.sr;
//   } else if (n == 23) {
//     *value = sh4->ctx.fpul;
//   } else if (n == 24) {
//     *value = sh4->ctx.fpscr;
//   } else if (n < 41) {
//     *value = sh4->ctx.fr[n - 25];
//   } else if (n == 41) {
//     *value = sh4->ctx.ssr;
//   } else if (n == 42) {
//     *value = sh4->ctx.spc;
//   } else if (n < 51) {
//     uint32_t *b0 = (sh4->ctx.sr & RB) ? sh4->ctx.ralt : sh4->ctx.r;
//     *value = b0[n - 43];
//   } else if (n < 59) {
//     uint32_t *b1 = (sh4->ctx.sr & RB) ? sh4->ctx.r : sh4->ctx.ralt;
//     *value = b1[n - 51];
//   }

//   *size = 4;
// }

void sh4_paint(sh4_t *sh4, bool show_main_menu) {
  sh4_perf_t *perf = &sh4->perf;

  if (show_main_menu && ImGui::BeginMainMenuBar()) {
    if (ImGui::BeginMenu("CPU")) {
      ImGui::MenuItem("Perf", "", &perf->show);
      ImGui::EndMenu();
    }

    ImGui::EndMainMenuBar();
  }

  if (perf->show) {
    ImGui::Begin("Perf", nullptr, ImGuiWindowFlags_NoTitleBar |
                                      ImGuiWindowFlags_NoResize |
                                      ImGuiWindowFlags_NoMove |
                                      ImGuiWindowFlags_AlwaysAutoResize);

    ImGui::SetWindowPos(ImVec2(
        ImGui::GetIO().DisplaySize.x - ImGui::GetWindowSize().x - 10, 10));

    // calculate average mips
    float avg_mips = 0.0f;
    for (int i = std::max(0, perf->num_mips - MAX_MIPS_SAMPLES);
         i < perf->num_mips; i++) {
      avg_mips += perf->mips[i % MAX_MIPS_SAMPLES];
    }
    avg_mips /= std::max(std::min(perf->num_mips, MAX_MIPS_SAMPLES), 1);

    char overlay_text[128];
    snprintf(overlay_text, sizeof(overlay_text), "%.2f", avg_mips);
    ImGui::PlotLines("MIPS", perf->mips, MAX_MIPS_SAMPLES, perf->num_mips,
                     overlay_text, 0.0f, 400.0f);

    ImGui::End();
  }
}

void sh4_raise_interrupt(sh4_t *sh4, sh4_interrupt_t intr) {
  sh4->requested_interrupts |= sh4->sort_id[intr];
  sh4_intc_update_pending(sh4);
}

void sh4_clear_interrupt(sh4_t *sh4, sh4_interrupt_t intr) {
  sh4->requested_interrupts &= ~sh4->sort_id[intr];
  sh4_intc_update_pending(sh4);
}

void sh4_ddt(sh4_t *sh4, sh4_dtr_t *dtr) {
  if (dtr->data) {
    // single address mode transfer
    if (dtr->rw) {
      address_space_memcpy_to_guest(sh4->space, dtr->addr, dtr->data,
                                    dtr->size);
    } else {
      address_space_memcpy_to_host(sh4->space, dtr->data, dtr->addr, dtr->size);
    }
  } else {
    // dual address mode transfer
    // NOTE this should be made asynchronous, at which point the significance
    // of the registers / interrupts should be more obvious
    uint32_t *sar;
    uint32_t *dar;
    uint32_t *dmatcr;
    chcr_t *chcr;
    sh4_interrupt_t dmte;

    switch (dtr->channel) {
      case 0:
        sar = sh4->SAR0;
        dar = sh4->DAR0;
        dmatcr = sh4->DMATCR0;
        chcr = sh4->CHCR0;
        dmte = SH4_INTC_DMTE0;
        break;
      case 1:
        sar = sh4->SAR1;
        dar = sh4->DAR1;
        dmatcr = sh4->DMATCR1;
        chcr = sh4->CHCR1;
        dmte = SH4_INTC_DMTE1;
        break;
      case 2:
        sar = sh4->SAR2;
        dar = sh4->DAR2;
        dmatcr = sh4->DMATCR2;
        chcr = sh4->CHCR2;
        dmte = SH4_INTC_DMTE2;
        break;
      case 3:
        sar = sh4->SAR3;
        dar = sh4->DAR3;
        dmatcr = sh4->DMATCR3;
        chcr = sh4->CHCR3;
        dmte = SH4_INTC_DMTE3;
        break;
      default:
        LOG_FATAL("Unexpected DMA channel");
        break;
    }

    uint32_t src = dtr->rw ? dtr->addr : *sar;
    uint32_t dst = dtr->rw ? *dar : dtr->addr;
    int size = *dmatcr * 32;
    address_space_memcpy(sh4->space, dst, src, size);

    // update src / addresses as well as remaining count
    *sar = src + size;
    *dar = dst + size;
    *dmatcr = 0;

    // signal transfer end
    chcr->TE = 1;

    // raise interrupt if requested
    if (chcr->IE) {
      sh4_raise_interrupt(sh4, dmte);
    }
  }
}

uint32_t sh4_compile_pc() {
  uint32_t guest_addr = g_sh4->ctx.pc;
  uint8_t *guest_ptr =
      address_space_translate(g_sh4->base.memory->space, guest_addr);
  int flags = sh4_compile_flags(g_sh4);

  code_pointer_t code =
      sh4_cache_compile_code(g_sh4->code_cache, guest_addr, guest_ptr, flags);
  return code();
}

void sh4_invalid_instr(sh4_context_t *ctx, uint64_t data) {
  // sh4_t *self = reinterpret_cast<SH4 *>(ctx->sh4);
  // uint32_t addr = static_cast<uint32_t>(data);

  // auto it = self->breakpoints.find(addr);
  // CHECK_NE(it, self->breakpoints.end());

  // // force the main loop to break
  // self->ctx.num_cycles = 0;

  // // let the debugger know execution has stopped
  // self->dc.base->debugger->Trap();
}

void sh4_prefetch(sh4_context_t *ctx, uint64_t data) {
  sh4_t *sh4 = reinterpret_cast<sh4_t *>(ctx->sh4);
  uint32_t addr = static_cast<uint32_t>(data);

  // only concerned about SQ related prefetches
  if (addr < 0xe0000000 || addr > 0xe3ffffff) {
    return;
  }

  // figure out the source and destination
  uint32_t dest = addr & 0x03ffffe0;
  uint32_t sqi = (addr & 0x20) >> 5;
  if (sqi) {
    dest |= (*sh4->QACR1 & 0x1c) << 24;
  } else {
    dest |= (*sh4->QACR0 & 0x1c) << 24;
  }

  // perform the "burst" 32-byte copy
  for (int i = 0; i < 8; i++) {
    address_space_w32(sh4->space, dest, sh4->ctx.sq[sqi][i]);
    dest += 4;
  }
}

void sh4_sr_updated(sh4_context_t *ctx, uint64_t old_sr) {
  sh4_t *sh4 = reinterpret_cast<sh4_t *>(ctx->sh4);

  if ((ctx->sr & RB) != (old_sr & RB)) {
    sh4_swap_gpr_bank(sh4);
  }

  if ((ctx->sr & I) != (old_sr & I) || (ctx->sr & BL) != (old_sr & BL)) {
    sh4_intc_update_pending(sh4);
  }
}

void sh4_fpscr_updated(sh4_context_t *ctx, uint64_t old_fpscr) {
  sh4_t *sh4 = reinterpret_cast<sh4_t *>(ctx->sh4);

  if ((ctx->fpscr & FR) != (old_fpscr & FR)) {
    sh4_swap_fpr_bank(sh4);
  }
}

int sh4_compile_flags(sh4_t *sh4) {
  int flags = 0;
  if (sh4->ctx.fpscr & PR) {
    flags |= SH4_DOUBLE_PR;
  }
  if (sh4->ctx.fpscr & SZ) {
    flags |= SH4_DOUBLE_SZ;
  }
  return flags;
}

void sh4_swap_gpr_bank(sh4_t *sh4) {
  for (int s = 0; s < 8; s++) {
    uint32_t tmp = sh4->ctx.r[s];
    sh4->ctx.r[s] = sh4->ctx.ralt[s];
    sh4->ctx.ralt[s] = tmp;
  }
}

void sh4_swap_fpr_bank(sh4_t *sh4) {
  for (int s = 0; s <= 15; s++) {
    uint32_t tmp = sh4->ctx.fr[s];
    sh4->ctx.fr[s] = sh4->ctx.xf[s];
    sh4->ctx.xf[s] = tmp;
  }
}

//
// CCN
//
void sh4_ccn_reset(sh4_t *sh4) {
  // FIXME this isn't right. When the IC is reset a pending flag is set and the
  // cache is actually reset at the end of the current block. However, the docs
  // for the SH4 IC state "After CCR is updated, an instruction that performs
  // data access to the P0, P1, P3, or U0 area should be located at least four
  // instructions after the CCR update instruction. Also, a branch instruction
  // to the P0, P1, P3, or U0 area should be located at least eight instructions
  // after the CCR update instruction."
  LOG_INFO("Reset instruction cache");

  sh4_cache_unlink_blocks(sh4->code_cache);
}

//
// DMAC
//
void sh4_dmac_check(sh4_t *sh4, int channel) {
  chcr_t *chcr = nullptr;

  switch (channel) {
    case 0:
      chcr = sh4->CHCR0;
      break;
    case 1:
      chcr = sh4->CHCR1;
      break;
    case 2:
      chcr = sh4->CHCR2;
      break;
    case 3:
      chcr = sh4->CHCR3;
      break;
    default:
      LOG_FATAL("Unexpected DMA channel");
      break;
  }

  CHECK(sh4->DMAOR->DDT || !sh4->DMAOR->DME || !chcr->DE,
        "Non-DDT DMA not supported");
}

//
// INTC
//

// Generate a sorted set of interrupts based on their priority. These sorted
// ids are used to represent all of the currently requested interrupts as a
// simple bitmask.
void sh4_intc_reprioritize(sh4_t *sh4) {
  uint64_t old = sh4->requested_interrupts;
  sh4->requested_interrupts = 0;

  for (int i = 0, n = 0; i < 16; i++) {
    // for even priorities, give precedence to lower id interrupts
    for (int j = NUM_SH_INTERRUPTS - 1; j >= 0; j--) {
      sh4_interrupt_info_t &int_info = sh4_interrupts[j];

      // get current priority for interrupt
      int priority = int_info.default_priority;
      if (int_info.ipr) {
        uint32_t ipr = sh4->reg[int_info.ipr];
        priority = ((ipr & 0xffff) >> int_info.ipr_shift) & 0xf;
      }

      if (priority != i) {
        continue;
      }

      bool was_requested = old & sh4->sort_id[j];

      sh4->sorted_interrupts[n] = (sh4_interrupt_t)j;
      sh4->sort_id[j] = (uint64_t)1 << n;
      n++;

      if (was_requested) {
        // rerequest with new sorted id
        sh4->requested_interrupts |= sh4->sort_id[j];
      }
    }

    // generate a mask for all interrupts up to the current priority
    sh4->priority_mask[i] = ((uint64_t)1 << n) - 1;
  }

  sh4_intc_update_pending(sh4);
}

void sh4_intc_update_pending(sh4_t *sh4) {
  int min_priority = (sh4->ctx.sr & I) >> 4;
  uint64_t priority_mask =
      (sh4->ctx.sr & BL) ? 0 : ~sh4->priority_mask[min_priority];
  sh4->pending_interrupts = sh4->requested_interrupts & priority_mask;
}

void sh4_intc_check_pending(sh4_t *sh4) {
  if (!sh4->pending_interrupts) {
    return;
  }

  // process the highest priority in the pending vector
  int n = 63 - clz64(sh4->pending_interrupts);
  sh4_interrupt_t intr = sh4->sorted_interrupts[n];
  sh4_interrupt_info_t &int_info = sh4_interrupts[intr];

  *sh4->INTEVT = int_info.intevt;
  sh4->ctx.ssr = sh4->ctx.sr;
  sh4->ctx.spc = sh4->ctx.pc;
  sh4->ctx.sgr = sh4->ctx.r[15];
  sh4->ctx.sr |= (BL | MD | RB);
  sh4->ctx.pc = sh4->ctx.vbr + 0x600;

  sh4_sr_updated(&sh4->ctx, sh4->ctx.ssr);
}

//
// TMU
//
static const int64_t PERIPHERAL_CLOCK_FREQ = SH4_CLOCK_FREQ >> 2;
static const int PERIPHERAL_SCALE[] = {2, 4, 6, 8, 10, 0, 0, 0};

#define TSTR(n) (*sh4->TSTR & (1 << n))
#define TCOR(n) (n == 0 ? sh4->TCOR0 : n == 1 ? sh4->TCOR1 : sh4->TCOR2)
#define TCNT(n) (n == 0 ? sh4->TCNT0 : n == 1 ? sh4->TCNT1 : sh4->TCNT2)
#define TCR(n) (n == 0 ? sh4->TCR0 : n == 1 ? sh4->TCR1 : sh4->TCR2)
#define TUNI(n) \
  (n == 0 ? SH4_INTC_TUNI0 : n == 1 ? SH4_INTC_TUNI1 : SH4_INTC_TUNI2)

void sh4_tmu_update_tstr(sh4_t *sh4) {
  for (int i = 0; i < 3; i++) {
    struct timer_s **timer = &sh4->tmu_timers[i];

    if (TSTR(i)) {
      // schedule the timer if not already started
      if (!*timer) {
        sh4_tmu_reschedule(sh4, i, *TCNT(i), *TCR(i));
      }
    } else if (*timer) {
      // disable the timer
      scheduler_cancel_timer(sh4->scheduler, *timer);
      *timer = nullptr;
    }
  }
}

void sh4_tmu_update_tcr(sh4_t *sh4, uint32_t n) {
  if (TSTR(n)) {
    // timer is already scheduled, reschedule it with the current cycle
    // count, but the new TCR value
    sh4_tmu_reschedule(sh4, n, sh4_tmu_tcnt(sh4, n), *TCR(n));
  }

  // if the timer no longer cares about underflow interrupts, unrequest
  if (!(*TCR(n) & 0x20) || !(*TCR(n) & 0x100)) {
    sh4_clear_interrupt(sh4, TUNI(n));
  }
}

void sh4_tmu_update_tcnt(sh4_t *sh4, uint32_t n) {
  if (TSTR(n)) {
    sh4_tmu_reschedule(sh4, n, *TCNT(n), *TCR(n));
  }
}

uint32_t sh4_tmu_tcnt(sh4_t *sh4, int n) {
  // TCNT values aren't updated in real time. if a timer is enabled, query
  // the scheduler to figure out how many cycles are remaining for the given
  // timer
  if (!TSTR(n)) {
    return *TCNT(n);
  }

  // FIXME should the number of SH4 cycles that've been executed be considered
  // here? this would prevent an entire SH4 slice from just busy waiting on
  // this to change

  struct timer_s *timer = sh4->tmu_timers[n];
  uint32_t tcr = *TCR(n);

  int64_t freq = PERIPHERAL_CLOCK_FREQ >> PERIPHERAL_SCALE[tcr & 7];
  int64_t remaining = scheduler_remaining_time(sh4->scheduler, timer);
  int64_t cycles = NANO_TO_CYCLES(remaining, freq);

  return static_cast<uint32_t>(cycles);
}

void sh4_tmu_reschedule(sh4_t *sh4, int n, uint32_t tcnt, uint32_t tcr) {
  struct timer_s **timer = &sh4->tmu_timers[n];

  int64_t freq = PERIPHERAL_CLOCK_FREQ >> PERIPHERAL_SCALE[tcr & 7];
  int64_t cycles = static_cast<int64_t>(tcnt);
  int64_t remaining = CYCLES_TO_NANO(cycles, freq);

  if (*timer) {
    scheduler_cancel_timer(sh4->scheduler, *timer);
    *timer = nullptr;
  }

  timer_cb cb =
      (timer_cb)(n == 0 ? &sh4_tmu_expire_0 : n == 1 ? &sh4_tmu_expire_1
                                                     : &sh4_tmu_expire_2);
  *timer = scheduler_start_timer(sh4->scheduler, cb, sh4, remaining);
}

void sh4_tmu_expire(sh4_t *sh4, int n) {
  uint32_t *tcor = TCOR(n);
  uint32_t *tcnt = TCNT(n);
  uint32_t *tcr = TCR(n);

  LOG_INFO("sh4_tmu_expire");

  // timer expired, set the underflow flag
  *tcr |= 0x100;

  // if interrupt generation on underflow is enabled, do so
  if (*tcr & 0x20) {
    sh4_raise_interrupt(sh4, TUNI(n));
  }

  // reset TCNT with the value from TCOR
  *tcnt = *tcor;

  // reschedule the timer with the new count
  sh4_tmu_reschedule(sh4, n, *tcnt, *tcr);
}

void sh4_tmu_expire_0(sh4_t *sh4) {
  sh4_tmu_expire(sh4, 0);
}

void sh4_tmu_expire_1(sh4_t *sh4) {
  sh4_tmu_expire(sh4, 1);
}

void sh4_tmu_expire_2(sh4_t *sh4) {
  sh4_tmu_expire(sh4, 2);
}

template <typename T>
T sh4_read_reg(sh4_t *sh4, uint32_t addr) {
  uint32_t offset = SH4_REG_OFFSET(addr);
  reg_read_cb read = sh4->reg_read[offset];

  if (read) {
    void *data = sh4->reg_data[offset];
    return read(data);
  }

  return static_cast<T>(sh4->reg[offset]);
}

template <typename T>
void sh4_write_reg(sh4_t *sh4, uint32_t addr, T value) {
  uint32_t offset = SH4_REG_OFFSET(addr);
  reg_write_cb write = sh4->reg_write[offset];

  uint32_t old_value = sh4->reg[offset];
  sh4->reg[offset] = static_cast<uint32_t>(value);

  if (write) {
    void *data = sh4->reg_data[offset];
    write(data, old_value, &sh4->reg[offset]);
  }
}

// with OIX, bit 25, rather than bit 13, determines which 4kb bank to use
#define CACHE_OFFSET(addr, OIX) \
  ((OIX ? ((addr & 0x2000000) >> 13) : ((addr & 0x2000) >> 1)) | (addr & 0xfff))

template <typename T>
T sh4_read_cache(sh4_t *sh4, uint32_t addr) {
  CHECK_EQ(sh4->CCR->ORA, 1u);
  addr = CACHE_OFFSET(addr, sh4->CCR->OIX);
  return load<T>(&sh4->cache[addr]);
}

template <typename T>
void sh4_write_cache(sh4_t *sh4, uint32_t addr, T value) {
  CHECK_EQ(sh4->CCR->ORA, 1u);
  addr = CACHE_OFFSET(addr, sh4->CCR->OIX);
  store(&sh4->cache[addr], value);
}

template <typename T>
T sh4_read_sq(sh4_t *sh4, uint32_t addr) {
  uint32_t sqi = (addr & 0x20) >> 5;
  unsigned idx = (addr & 0x1c) >> 2;
  return static_cast<T>(sh4->ctx.sq[sqi][idx]);
}

template <typename T>
void sh4_write_sq(sh4_t *sh4, uint32_t addr, T value) {
  uint32_t sqi = (addr & 0x20) >> 5;
  uint32_t idx = (addr & 0x1c) >> 2;
  sh4->ctx.sq[sqi][idx] = static_cast<uint32_t>(value);
}

REG_R32(sh4_t *sh4, PDTRA) {
  // magic values to get past 0x8c00b948 in the boot rom:
  // void _8c00b92c(int arg1) {
  //   sysvars->var1 = reg[PDTRA];
  //   for (i = 0; i < 4; i++) {
  //     sysvars->var2 = reg[PDTRA];
  //     if (arg1 == sysvars->var2 & 0x03) {
  //       return;
  //     }
  //   }
  //   reg[PR] = (uint32_t *)0x8c000000;    /* loop forever */
  // }
  // old_PCTRA = reg[PCTRA];
  // i = old_PCTRA | 0x08;
  // reg[PCTRA] = i;
  // reg[PDTRA] = reg[PDTRA] | 0x03;
  // _8c00b92c(3);
  // reg[PCTRA] = i | 0x03;
  // _8c00b92c(3);
  // reg[PDTRA] = reg[PDTRA] & 0xfffe;
  // _8c00b92c(0);
  // reg[PCTRA] = i;
  // _8c00b92c(3);
  // reg[PCTRA] = i | 0x04;
  // _8c00b92c(3);
  // reg[PDTRA] = reg[PDTRA] & 0xfffd;
  // _8c00b92c(0);
  // reg[PCTRA] = old_PCTRA;
  uint32_t pctra = *sh4->PCTRA;
  uint32_t pdtra = *sh4->PDTRA;
  uint32_t v = 0;
  if ((pctra & 0xf) == 0x8 || ((pctra & 0xf) == 0xb && (pdtra & 0xf) != 0x2) ||
      ((pctra & 0xf) == 0xc && (pdtra & 0xf) == 0x2)) {
    v = 3;
  }
  // FIXME cable setting
  // When a VGA cable* is connected
  // 1. The SH4 obtains the cable information from the PIO port.  (PB[9:8] =
  // "00")
  // 2. Set the HOLLY synchronization register for VGA.  (The SYNC output is
  // H-Sync and V-Sync.)
  // 3. When VREG1 = 0 and VREG0 = 0 are written in the AICA register,
  // VIDEO1 = 0 and VIDEO0 = 1 are output.  VIDEO0 is connected to the
  // DVE-DACH pin, and handles switching between RGB and NTSC/PAL.
  //
  // When an RGB(NTSC/PAL) cable* is connected
  // 1. The SH4 obtains the cable information from the PIO port.  (PB[9:8] =
  // "10")
  // 2. Set the HOLLY synchronization register for NTSC/PAL.  (The SYNC
  // output is H-Sync and V-Sync.)
  // 3. When VREG1 = 0 and VREG0 = 0 are written in the AICA register,
  // VIDEO1 = 1 and VIDEO0 = 0 are output.  VIDEO0 is connected to the
  // DVE-DACH pin, and handles switching between RGB and NTSC/PAL.
  //
  // When a stereo A/V cable, an S-jack cable* or an RF converter* is
  // connected
  // 1. The SH4 obtains the cable information from the PIO port.  (PB[9:8] =
  // "11")
  // 2. Set the HOLLY synchronization register for NTSC/PAL.  (The SYNC
  // output is H-Sync and V-Sync.)
  // 3. When VREG1 = 1 and VREG0 = 1 are written in the AICA register,
  // VIDEO1 = 0 and VIDEO0 = 0 are output.  VIDEO0 is connected to the
  // DVE-DACH pin, and handles switching between RGB and NTSC/PAL.
  // v |= 0x3 << 8;
  return v;
}

REG_W32(sh4_t *sh4, MMUCR) {
  if (!*new_value) {
    return;
  }

  LOG_FATAL("MMU not currently supported");
}

REG_W32(sh4_t *sh4, CCR) {
  if (sh4->CCR->ICI) {
    sh4_ccn_reset(sh4);
  }
}

REG_W32(sh4_t *sh4, CHCR0) {
  sh4_dmac_check(sh4, 0);
}

REG_W32(sh4_t *sh4, CHCR1) {
  sh4_dmac_check(sh4, 1);
}

REG_W32(sh4_t *sh4, CHCR2) {
  sh4_dmac_check(sh4, 2);
}

REG_W32(sh4_t *sh4, CHCR3) {
  sh4_dmac_check(sh4, 3);
}

REG_W32(sh4_t *sh4, DMAOR) {
  sh4_dmac_check(sh4, 0);
  sh4_dmac_check(sh4, 1);
  sh4_dmac_check(sh4, 2);
  sh4_dmac_check(sh4, 3);
}

REG_W32(sh4_t *sh4, IPRA) {
  sh4_intc_reprioritize(sh4);
}

REG_W32(sh4_t *sh4, IPRB) {
  sh4_intc_reprioritize(sh4);
}

REG_W32(sh4_t *sh4, IPRC) {
  sh4_intc_reprioritize(sh4);
}

REG_W32(sh4_t *sh4, TSTR) {
  sh4_tmu_update_tstr(sh4);
}

REG_W32(sh4_t *sh4, TCR0) {
  sh4_tmu_update_tcr(sh4, 0);
}

REG_W32(sh4_t *sh4, TCR1) {
  sh4_tmu_update_tcr(sh4, 1);
}

REG_W32(sh4_t *sh4, TCR2) {
  sh4_tmu_update_tcr(sh4, 1);
}

REG_R32(sh4_t *sh4, TCNT0) {
  return sh4_tmu_tcnt(sh4, 0);
}

REG_W32(sh4_t *sh4, TCNT0) {
  sh4_tmu_update_tcnt(sh4, 0);
}

REG_R32(sh4_t *sh4, TCNT1) {
  return sh4_tmu_tcnt(sh4, 1);
}

REG_W32(sh4_t *sh4, TCNT1) {
  sh4_tmu_update_tcnt(sh4, 1);
}

REG_R32(sh4_t *sh4, TCNT2) {
  return sh4_tmu_tcnt(sh4, 2);
}

REG_W32(sh4_t *sh4, TCNT2) {
  sh4_tmu_update_tcnt(sh4, 2);
}
