#include "hw/gdrom/gdrom.h"
#include "hw/gdrom/gdrom_replies.inc"
#include "hw/holly/holly.h"
#include "hw/dreamcast.h"
#include "hw/memory.h"

using namespace re;
using namespace re::hw;
using namespace re::hw::gdrom;
using namespace re::hw::holly;
using namespace re::hw::sh4;

#define SWAP_24(fad) \
  (((fad & 0xff) << 16) | (fad & 0x00ff00) | ((fad & 0xff0000) >> 16))

GDROM::GDROM(Dreamcast *dc)
    : Device(*dc),
      dc_(dc),
      memory_(nullptr),
      holly_(nullptr),
      holly_regs_(nullptr),
      features_{0},
      intreason_{0},
      sectnum_{0},
      byte_count_{0},
      status_{0},
      pio_idx_(0),
      pio_size_(0),
      dma_size_(0),
      state_(STATE_STANDBY),
      current_disc_(nullptr) {
  dma_buffer_ = new uint8_t[0x1000000];
}

GDROM::~GDROM() { delete[] dma_buffer_; }

bool GDROM::Init() {
  memory_ = dc_->memory;
  holly_ = dc_->holly;
  holly_regs_ = dc_->holly_regs;

  SetDisc(nullptr);

  return true;
}

void GDROM::SetDisc(std::unique_ptr<Disc> disc) {
  current_disc_ = std::move(disc);

  // looking at "6.1.1 CD Drive State Transition Diagram" in CDIF131E.pdf, it
  // seems standby is the default state for when a disc is inserted
  sectnum_.status = current_disc_ ? GD_STATUS_STANDBY : GD_STATUS_NODISC;
  sectnum_.format = GD_GDROM;

  status_.full = 0;
  status_.DRDY = 1;
  status_.BSY = 0;
}

template uint8_t GDROM::ReadRegister(uint32_t addr);
template uint16_t GDROM::ReadRegister(uint32_t addr);
template uint32_t GDROM::ReadRegister(uint32_t addr);
template <typename T>
T GDROM::ReadRegister(uint32_t addr) {
  uint32_t offset = addr >> 2;
  Register &reg = holly_regs_[offset];

  if (!(reg.flags & R)) {
    LOG_WARNING("Invalid read access at 0x%x", addr);
    return 0;
  }

  switch (offset) {
    // gdrom regs
    case GD_ALTSTAT_DEVCTRL_OFFSET:
      // this register is the same as the status register, but it does not
      // clear DMA status information when it is accessed
      return status_.full;

    case GD_DATA_OFFSET: {
      // TODO add ReadData function
      uint16_t v = *(uint16_t *)&pio_buffer_[pio_idx_];
      pio_idx_ += 2;
      if (pio_idx_ == pio_size_) {
        TriggerEvent(EV_SPI_WRITE_END);
      }
      return static_cast<T>(v);
    }

    case GD_ERROR_FEATURES_OFFSET:
      // LOG_INFO("GD_ERROR");
      return 0;

    case GD_INTREASON_SECTCNT_OFFSET:
      return intreason_.full;

    case GD_SECTNUM_OFFSET:
      return sectnum_.full;

    case GD_BYCTLLO_OFFSET:
      return byte_count_.lo;

    case GD_BYCTLHI_OFFSET:
      return byte_count_.hi;

    case GD_DRVSEL_OFFSET:
      // LOG_INFO("GD_DRVSEL");
      return 0;

    case GD_STATUS_COMMAND_OFFSET:
      holly_->UnrequestInterrupt(HOLLY_INTC_G1GDINT);
      return status_.full;

      // g1 bus regs
  }

  return reg.value;
}

template void GDROM::WriteRegister(uint32_t addr, uint8_t value);
template void GDROM::WriteRegister(uint32_t addr, uint16_t value);
template void GDROM::WriteRegister(uint32_t addr, uint32_t value);
template <typename T>
void GDROM::WriteRegister(uint32_t addr, T value) {
  uint32_t offset = addr >> 2;
  Register &reg = holly_regs_[offset];

  if (!(reg.flags & W)) {
    LOG_WARNING("Invalid write access at 0x%x", addr);
    return;
  }

  uint32_t old_value = reg.value;
  reg.value = static_cast<uint32_t>(value);

  switch (offset) {
    // gdrom regs
    case GD_ALTSTAT_DEVCTRL_OFFSET:
      // LOG_INFO("GD_DEVCTRL 0x%x", (uint32_t)value);
      break;

    case GD_DATA_OFFSET: {
      // TODO add WriteData function
      *(uint16_t *)(&pio_buffer_[pio_idx_]) = reg.value & 0xffff;
      pio_idx_ += 2;
      if ((state_ == STATE_SPI_READ_CMD && pio_idx_ == 12) ||
          (state_ == STATE_SPI_READ_DATA && pio_idx_ == pio_size_)) {
        TriggerEvent(EV_SPI_READ_END);
      }
    } break;

    case GD_ERROR_FEATURES_OFFSET:
      features_.full = reg.value;
      break;

    case GD_INTREASON_SECTCNT_OFFSET:
      // LOG_INFO("GD_SECTCNT 0x%x", reg.value);
      break;

    case GD_SECTNUM_OFFSET:
      sectnum_.full = reg.value;
      break;

    case GD_BYCTLLO_OFFSET:
      byte_count_.lo = reg.value;
      break;

    case GD_BYCTLHI_OFFSET:
      byte_count_.hi = reg.value;
      break;

    case GD_DRVSEL_OFFSET:
      // LOG_INFO("GD_DRVSEL 0x%x", (uint32_t)reg.value);
      break;

    case GD_STATUS_COMMAND_OFFSET:
      ProcessATACommand((ATACommand)reg.value);
      break;

    // g1 bus regs
    case SB_GDEN_OFFSET:
      // NOTE for when this is made asynchtonous
      // This register can also be used to forcibly terminate such a DMA
      // transfer that is in progress, by writing a "0" to this register.
      break;

    case SB_GDST_OFFSET:
      // if a "0" is written to this register, it is ignored
      reg.value |= old_value;

      if (reg.value) {
        // TODO add DMAStart function

        // NOTE for when this is made asynchronous
        // Cautions during DMA operations: If the SB_GDAPRO, SB_G1GDRC,
        // SB_GDSTAR, SB_GDLEN, or SB_GDDIR register is overwritten while a DMA
        // operation is in progress, the new setting has no effect on the
        // current DMA operation.
        CHECK_EQ(dc_->SB_GDEN, 1);   // dma enabled
        CHECK_EQ(dc_->SB_GDDIR, 1);  // gd-rom -> system memory
        CHECK_EQ(dc_->SB_GDLEN, (uint32_t)dma_size_);

        int transfer_size = dc_->SB_GDLEN;
        uint32_t start = dc_->SB_GDSTAR;

        LOG_INFO("GD DMA START 0x%x -> 0x%x, 0x%x bytes", start,
                 start + transfer_size, transfer_size);

        memory_->Memcpy(start, dma_buffer_, transfer_size);

        // done
        dc_->SB_GDSTARD = start + transfer_size;
        dc_->SB_GDLEND = transfer_size;
        dc_->SB_GDST = 0;
        holly_->RequestInterrupt(HOLLY_INTC_G1DEINT);

        // finish off CD_READ command
        TriggerEvent(EV_SPI_CMD_DONE);
      }
      break;
  }
}

void GDROM::TriggerEvent(GDEvent ev) { TriggerEvent(ev, 0, 0); }

void GDROM::TriggerEvent(GDEvent ev, intptr_t arg0, intptr_t arg1) {
  switch (ev) {
    case EV_ATA_CMD_DONE: {
      CHECK_EQ(state_, STATE_STANDBY);

      status_.DRDY = 1;
      status_.BSY = 0;

      holly_->RequestInterrupt(HOLLY_INTC_G1GDINT);

      state_ = STATE_STANDBY;
    } break;

    case EV_SPI_WAIT_CMD: {
      CHECK_EQ(state_, STATE_STANDBY);

      pio_idx_ = 0;

      intreason_.CoD = 1;
      intreason_.IO = 0;
      status_.DRQ = 1;
      status_.BSY = 0;

      state_ = STATE_SPI_READ_CMD;
    } break;

    case EV_SPI_READ_START: {
      CHECK_EQ(state_, STATE_SPI_READ_CMD);

      int offset = static_cast<int>(arg0);
      int size = static_cast<int>(arg1);
      CHECK_NE(size, 0);

      pio_idx_ = 0;
      pio_size_ = size;
      spi_read_offset_ = offset;

      byte_count_.full = size;
      intreason_.IO = 1;
      intreason_.CoD = 0;
      status_.DRQ = 1;
      status_.BSY = 0;

      holly_->RequestInterrupt(HOLLY_INTC_G1GDINT);

      state_ = STATE_SPI_READ_DATA;
    } break;

    case EV_SPI_READ_END: {
      CHECK(state_ == STATE_SPI_READ_CMD || state_ == STATE_SPI_READ_DATA);

      if (state_ == STATE_SPI_READ_CMD) {
        ProcessSPICommand(pio_buffer_);
      } else if (state_ == STATE_SPI_READ_DATA) {
        // only SPI_SET_MODE uses this
        memcpy(reinterpret_cast<uint8_t *>(&reply_11[spi_read_offset_ >> 1]),
               pio_buffer_, pio_size_);
        TriggerEvent(EV_SPI_CMD_DONE);
      }
    } break;

    case EV_SPI_WRITE_START: {
      CHECK_EQ(state_, STATE_SPI_READ_CMD);

      uint8_t *data = reinterpret_cast<uint8_t *>(arg0);
      int size = static_cast<int>(arg1);
      CHECK_NE(size, 0);

      memcpy(pio_buffer_, data, size);
      pio_size_ = size;
      pio_idx_ = 0;

      byte_count_.full = size;
      intreason_.IO = 1;
      intreason_.CoD = 0;
      status_.DRQ = 1;
      status_.BSY = 0;

      holly_->RequestInterrupt(HOLLY_INTC_G1GDINT);

      state_ = STATE_SPI_WRITE_DATA;
    } break;

    case EV_SPI_WRITE_END: {
      CHECK_EQ(state_, STATE_SPI_WRITE_DATA);

      TriggerEvent(EV_SPI_CMD_DONE);
    } break;

    case EV_SPI_CMD_DONE: {
      CHECK(state_ == STATE_SPI_READ_CMD || state_ == STATE_SPI_READ_DATA ||
            state_ == STATE_SPI_WRITE_DATA);

      intreason_.IO = 1;
      intreason_.CoD = 1;
      status_.DRDY = 1;
      status_.BSY = 0;
      status_.DRQ = 0;

      holly_->RequestInterrupt(HOLLY_INTC_G1GDINT);

      state_ = STATE_STANDBY;
    } break;
  }
}

void GDROM::ProcessATACommand(ATACommand cmd) {
  status_.DRDY = 0;
  status_.BSY = 1;

  switch (cmd) {
    // case ATA_NOP:
    //   // Setting "abort" in the error register
    //   // Setting "error" in the status register
    //   // Clearing BUSY in the status register
    //   // Asserting the INTRQ signal
    //   TriggerEvent(EV_ATA_CMD_DONE);
    //   break;

    case ATA_SOFT_RESET:
      SetDisc(std::move(current_disc_));
      TriggerEvent(EV_ATA_CMD_DONE);
      break;

    // case ATA_EXEC_DIAG:
    //   LOG_FATAL("Unhandled");
    //   break;

    case ATA_PACKET:
      TriggerEvent(EV_SPI_WAIT_CMD);
      break;

    // case ATA_IDENTIFY_DEV:
    //   LOG_FATAL("Unhandled");
    //   break;

    case ATA_SET_FEATURES:
      // FIXME I think we're supposed to be honoring GD_SECTCNT here to control
      // the DMA setting used by CD_READ
      TriggerEvent(EV_ATA_CMD_DONE);
      break;

    default:
      LOG_FATAL("Unsupported ATA command %d", cmd);
      break;
  }
}

void GDROM::ProcessSPICommand(uint8_t *data) {
  SPICommand cmd = (SPICommand)data[0];

  status_.DRQ = 0;
  status_.BSY = 1;

  switch (cmd) {
    //
    // Packet Command Flow For PIO DATA To Host
    //
    // case SPI_REQ_STAT:
    //   LOG_FATAL("Unhandled");
    //   break;

    case SPI_REQ_MODE: {
      int addr = data[2];
      int sz = data[4];
      TriggerEvent(EV_SPI_WRITE_START, (intptr_t)&reply_11[addr >> 1], sz);
    } break;

    // case SPI_REQ_ERROR:
    //   LOG_FATAL("Unhandled");
    //   break;

    case SPI_GET_TOC: {
      AreaType area_type = (AreaType)(data[1] & 0x1);
      int size = (data[3] << 8) | data[4];
      TOC toc;
      GetTOC(area_type, &toc);
      TriggerEvent(EV_SPI_WRITE_START, (intptr_t)&toc, size);
    } break;

    case SPI_REQ_SES: {
      int session = data[2];
      int size = data[4];
      Session ses;
      GetSession(session, &ses);
      TriggerEvent(EV_SPI_WRITE_START, (intptr_t)&ses, size);
    } break;

    case SPI_GET_SCD: {
      int format = data[1] & 0xffff;
      int size = (data[3] << 8) | data[4];
      uint8_t scd[SUBCODE_SIZE];
      GetSubcode(format, scd);
      TriggerEvent(EV_SPI_WRITE_START, (intptr_t)scd, size);
    } break;

    case SPI_CD_READ: {
      auto GetFAD = [](uint8_t a, uint8_t b, uint8_t c, bool msf) {
        if (msf) {
          // MSF mode
          // Byte 2 - Start time: minutes (binary 0 - 255)
          // Byte 3 - Start time: seconds (binary 0 - 59)
          // Byte 4 - Start time: frames (binary 0 - 74)
          return (a * 60 * 75) + (b * 75) + c;
        }

        // FAD mode
        // Byte 2 - Start frame address (MSB)
        // Byte 3 - Start frame address
        // Byte 4- Start frame address (LSB)
        return (a << 16) | (b << 8) | c;
      };

      bool use_dma = features_.dma;
      bool use_msf = (data[1] & 0x1);
      SectorFormat expected_format = (SectorFormat)((data[1] & 0xe) >> 1);
      DataMask data_mask = (DataMask)((data[1] >> 4) & 0xff);
      int start_addr = GetFAD(data[2], data[3], data[4], use_msf);
      int num_sectors = (data[8] << 16) | (data[9] << 8) | data[10];

      CHECK_EQ(expected_format, SECTOR_M1);

      if (use_dma) {
        int r = ReadSectors(start_addr, expected_format, data_mask, num_sectors,
                            dma_buffer_);
        dma_size_ = r;
      } else {
        LOG_FATAL("Unsupported non-dma CD read");
      }
    } break;

    // case SPI_CD_READ2:
    //   LOG_FATAL("Unhandled");
    //   break;

    //
    // Transfer Packet Command Flow For PIO Data from Host
    //
    case SPI_SET_MODE: {
      int offset = data[2];
      int size = data[4];
      TriggerEvent(EV_SPI_READ_START, offset, size);
    } break;

    //
    // Non-Data Command Flow
    //
    case SPI_TEST_UNIT:
      TriggerEvent(EV_SPI_CMD_DONE);
      break;

    // case SPI_CD_OPEN:
    // case SPI_CD_PLAY:
    // case SPI_CD_SEEK:
    // case SPI_CD_SCAN:
    //   TriggerEvent(EV_SPI_CMD_DONE);
    //   break;

    case SPI_UNKNOWN_70:
      TriggerEvent(EV_SPI_CMD_DONE);
      break;

    case SPI_UNKNOWN_71:
      TriggerEvent(EV_SPI_WRITE_START, (intptr_t)reply_71, sizeof(reply_71));
      break;

    default:
      LOG_FATAL("Unsupported SPI command %d", cmd);
      break;
  }
}

void GDROM::GetTOC(AreaType area_type, TOC *toc) {
  CHECK(current_disc_);

  // for GD-ROMs, the single density area contains tracks 1 and 2, while the
  // dual density area contains tracks 3 - num_tracks
  int first_track_num = 0;
  int last_track_num = current_disc_->num_tracks() - 1;

  // TODO conditionally check current_disc_ to make sure it's a GD-ROM once
  // CD-ROMs are supported
  if (1 /* is gd-rom */) {
    if (area_type == AREA_SINGLE_DENSITY) {
      last_track_num = 1;
    } else {
      first_track_num = 2;
    }
  }

  const Track &start_track = current_disc_->track(first_track_num);
  const Track &end_track = current_disc_->track(last_track_num);
  int leadout_fad = area_type == AREA_SINGLE_DENSITY ? 0x4650 : 0x861b4;

  memset(toc, 0, sizeof(*toc));
  for (int i = first_track_num; i <= last_track_num; i++) {
    TOCEntry &entry = toc->entries[i];
    const Track &track = current_disc_->track(i);
    entry.ctrl = track.ctrl;
    entry.adr = track.adr;
    entry.fad = track.num;
  }
  toc->start.ctrl = start_track.ctrl;
  toc->start.adr = start_track.adr;
  toc->start.fad = SWAP_24(end_track.fad);
  toc->end.ctrl = end_track.ctrl;
  toc->end.adr = end_track.adr;
  toc->end.fad = SWAP_24(end_track.fad);
  toc->leadout.ctrl = 0;
  toc->leadout.adr = 0;
  toc->leadout.fad = SWAP_24(leadout_fad);
}

void GDROM::GetSession(int session, Session *ses) {
  CHECK(current_disc_);

  if (!session) {
    // session values have a different meaning for session == 0

    // TODO start_fad for non GD-Roms I guess is 0x4650
    if (1 /* is gd-rom */) {
      ses->first_track = 2;               // num sessions
      ses->start_fad = SWAP_24(0x861b4);  // end fad
    }
  } else if (session == 1) {
    ses->first_track = 1;
    ses->start_fad = SWAP_24(current_disc_->track(0).fad);
  } else if (session == 2) {
    ses->first_track = 3;
    ses->start_fad = SWAP_24(current_disc_->track(2).fad);
  }
}

void GDROM::GetSubcode(int format, uint8_t *data) {
  CHECK(current_disc_);

  // FIXME implement
  memset(data, 0, SUBCODE_SIZE);
  data[1] = AUDIO_NOSTATUS;

  LOG_INFO("GetSubcode not fully implemented");
}

int GDROM::ReadSectors(int fad, SectorFormat format, DataMask mask,
                       int num_sectors, uint8_t *dst) {
  CHECK(current_disc_);

  int total = 0;
  char data[SECTOR_SIZE];

  LOG_INFO("ReadSectors %d -> %d", fad, fad + num_sectors);

  for (int i = 0; i < num_sectors; i++) {
    int r = current_disc_->ReadSector(fad, data);
    CHECK_EQ(r, 1);

    if (format == SECTOR_M1 && mask == MASK_DATA) {
      CHECK_LT(total + 2048, 0x1000000);
      memcpy(dst, data + 16, 2048);
      dst += 2048;
      total += 2048;
      fad++;
    } else {
      CHECK(false);
    }
  }

  return total;
}
