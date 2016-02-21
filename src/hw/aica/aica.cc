#include "core/memory.h"
#include "hw/aica/aica.h"
#include "hw/dreamcast.h"
#include "hw/memory.h"

using namespace re::hw;
using namespace re::hw::aica;
using namespace re::hw::holly;

AICA::AICA(Dreamcast *dc) : Device(*dc), MemoryInterface(this), dc_(dc) {}

bool AICA::Init() {
  // aica_regs_ = dc_->aica_regs();
  wave_ram_ = dc_->memory->TranslateVirtual(WAVE_RAM_START);

  return true;
}

void AICA::MapPhysicalMemory(Memory &memory, MemoryMap &memmap) {
  // RegionHandle aica_reg_handle = memory.AllocRegion(
  //   AICA_REG_START, AICA_REG_SIZE, aica(),
  //   nullptr,
  //   nullptr,
  //   &AICA::ReadRegister,
  //   nullptr,
  //   nullptr,
  //   nullptr,
  //   &AICA::WriteRegister,
  //   nullptr);

  RegionHandle wave_ram_handle = memory.AllocRegion(
      WAVE_RAM_START, WAVE_RAM_SIZE,
      make_delegate(&AICA::ReadWave<uint8_t>, this),
      make_delegate(&AICA::ReadWave<uint16_t>, this),
      make_delegate(&AICA::ReadWave<uint32_t>, this), nullptr,
      make_delegate(&AICA::WriteWave<uint8_t>, this),
      make_delegate(&AICA::WriteWave<uint16_t>, this),
      make_delegate(&AICA::WriteWave<uint32_t>, this), nullptr);

  // memmap.Mount(aica_reg_handle, AICA_REG_SIZE, AICA_REG_START);
  memmap.Mount(wave_ram_handle, WAVE_RAM_SIZE, WAVE_RAM_START);
}

// frequency 22579200
// int AICA::Run(int cycles) {
//   // uint16_t MCIEB = re::load<uint16_t>(&aica_regs_[MCIEB_OFFSET]);
//   // uint16_t MCIPD = re::load<uint16_t>(&aica_regs_[MCIPD_OFFSET]);

//   // if (MCIEB || MCIPD) {
//   //   LOG_INFO("0x%x & 0x%x", MCIEB, MCIPD);
//   // }
//   // dc_->holly()->RequestInterrupt(HOLLY_INTC_G2AICINT);

//   return cycles;
// }

// uint32_t AICA::ReadRegister(void *ctx, uint32_t addr) {
//   AICA *self = reinterpret_cast<AICA *>(ctx);
//   // LOG_INFO("AICA::ReadRegister32 0x%x", addr);
//   return re::load<uint32_t>(&self->aica_regs_[addr]);
// }

// void AICA::WriteRegister(void *ctx, uint32_t addr, uint32_t value) {
//   AICA *self = reinterpret_cast<AICA *>(ctx);
//   // LOG_INFO("AICA::WriteRegister32 0x%x", addr);
//   re::store(&self->aica_regs_[addr], value);
// }

template <typename T>
T AICA::ReadWave(uint32_t addr) {
  if (sizeof(T) == 4) {
    // FIXME temp hacks to get Crazy Taxi 1 booting
    if (addr == 0x104 || addr == 0x284 || addr == 0x288) {
      return 0x54494e49;
    }
    // FIXME temp hacks to get Crazy Taxi 2 booting
    if (addr == 0x5c) {
      return 0x54494e49;
    }
  }

  return re::load<T>(&wave_ram_[addr]);
}

template <typename T>
void AICA::WriteWave(uint32_t addr, T value) {
  re::store(&wave_ram_[addr], value);
}
