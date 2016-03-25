#include "core/memory.h"
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

GDROM::GDROM(Dreamcast &dc)
    : Device(dc),
      dc_(dc),
      memory_(nullptr),
      holly_(nullptr),
      features_{0},
      intreason_{0},
      sectnum_{0},
      byte_count_{0},
      status_{0},
      pio_head_(0),
      pio_size_(0),
      dma_head_(0),
      dma_size_(0),
      state_(STATE_STANDBY),
      current_disc_(nullptr) {}

bool GDROM::Init() {
  memory_ = dc_.memory;
  holly_ = dc_.holly;

  GDROM_REGISTER_R32_DELEGATE(GD_ALTSTAT_DEVCTRL);
  GDROM_REGISTER_W32_DELEGATE(GD_ALTSTAT_DEVCTRL);
  GDROM_REGISTER_R32_DELEGATE(GD_DATA);
  GDROM_REGISTER_W32_DELEGATE(GD_DATA);
  GDROM_REGISTER_R32_DELEGATE(GD_ERROR_FEATURES);
  GDROM_REGISTER_W32_DELEGATE(GD_ERROR_FEATURES);
  GDROM_REGISTER_R32_DELEGATE(GD_INTREASON_SECTCNT);
  GDROM_REGISTER_W32_DELEGATE(GD_INTREASON_SECTCNT);
  GDROM_REGISTER_R32_DELEGATE(GD_SECTNUM);
  GDROM_REGISTER_W32_DELEGATE(GD_SECTNUM);
  GDROM_REGISTER_R32_DELEGATE(GD_BYCTLLO);
  GDROM_REGISTER_W32_DELEGATE(GD_BYCTLLO);
  GDROM_REGISTER_R32_DELEGATE(GD_BYCTLHI);
  GDROM_REGISTER_W32_DELEGATE(GD_BYCTLHI);
  GDROM_REGISTER_R32_DELEGATE(GD_DRVSEL);
  GDROM_REGISTER_W32_DELEGATE(GD_DRVSEL);
  GDROM_REGISTER_R32_DELEGATE(GD_STATUS_COMMAND);
  GDROM_REGISTER_W32_DELEGATE(GD_STATUS_COMMAND);

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

void GDROM::BeginDMA() {}

int GDROM::ReadDMA(uint8_t *data, int data_size) {
  int remaining = dma_size_ - dma_head_;
  int n = std::min(remaining, data_size);
  memcpy(data, &dma_buffer_[dma_head_], n);
  dma_head_ += n;
  return n;
}

void GDROM::EndDMA() {
  // reset DMA write state
  dma_size_ = 0;

  // CD_READ command is now done
  TriggerEvent(EV_SPI_CMD_DONE);
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

      pio_head_ = 0;

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

      pio_head_ = 0;
      pio_size_ = size;
      pio_read_offset_ = offset;

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
        CHECK_EQ(pio_head_, SPI_CMD_SIZE);
        ProcessSPICommand(pio_buffer_);
      } else if (state_ == STATE_SPI_READ_DATA) {
        ProcessSetMode(pio_read_offset_, pio_buffer_, pio_head_);
      }
    } break;

    case EV_SPI_WRITE_START: {
      CHECK_EQ(state_, STATE_SPI_READ_CMD);

      uint8_t *data = reinterpret_cast<uint8_t *>(arg0);
      int size = static_cast<int>(arg1);

      CHECK(size && size < static_cast<int>(sizeof(pio_buffer_)));
      memcpy(pio_buffer_, data, size);
      pio_size_ = size;
      pio_head_ = 0;

      byte_count_.full = pio_size_;
      intreason_.IO = 1;
      intreason_.CoD = 0;
      status_.DRQ = 1;
      status_.BSY = 0;

      holly_->RequestInterrupt(HOLLY_INTC_G1GDINT);

      state_ = STATE_SPI_WRITE_DATA;
    } break;

    case EV_SPI_WRITE_SECTORS: {
      CHECK(state_ == STATE_SPI_READ_CMD || state_ == STATE_SPI_WRITE_SECTORS);

      if (cdreq_.dma) {
        int max_dma_size = cdreq_.num_sectors * SECTOR_SIZE;

        // reserve the worst case size
        dma_buffer_.Reserve(max_dma_size);

        // read to DMA buffer
        dma_size_ = ReadSectors(cdreq_.first_sector, cdreq_.sector_format,
                                cdreq_.sector_mask, cdreq_.num_sectors,
                                dma_buffer_.data(), dma_buffer_.capacity());
        dma_head_ = 0;

        // gdrom state won't be updated until DMA transfer is completed
      } else {
        int max_pio_sectors = sizeof(pio_buffer_) / SECTOR_SIZE;

        // fill PIO buffer with as many sectors as possible
        int num_sectors = std::min(cdreq_.num_sectors, max_pio_sectors);
        pio_size_ = ReadSectors(cdreq_.first_sector, cdreq_.sector_format,
                                cdreq_.sector_mask, num_sectors, pio_buffer_,
                                sizeof(pio_buffer_));
        pio_head_ = 0;

        // update sector read state
        cdreq_.first_sector += num_sectors;
        cdreq_.num_sectors -= num_sectors;

        // update gdrom state
        byte_count_.full = pio_size_;
        intreason_.IO = 1;
        intreason_.CoD = 0;
        status_.DRQ = 1;
        status_.BSY = 0;

        holly_->RequestInterrupt(HOLLY_INTC_G1GDINT);
      }

      state_ = STATE_SPI_WRITE_SECTORS;
    } break;

    case EV_SPI_WRITE_END: {
      CHECK(state_ == STATE_SPI_WRITE_DATA ||
            state_ == STATE_SPI_WRITE_SECTORS);

      // if there are still sectors remaining to be written out to the PIO
      // buffer, continue doing so
      if (state_ == STATE_SPI_WRITE_SECTORS && cdreq_.num_sectors) {
        TriggerEvent(EV_SPI_WRITE_SECTORS);
      } else {
        TriggerEvent(EV_SPI_CMD_DONE);
      }
    } break;

    case EV_SPI_CMD_DONE: {
      CHECK(state_ == STATE_SPI_READ_CMD || state_ == STATE_SPI_READ_DATA ||
            state_ == STATE_SPI_WRITE_DATA ||
            state_ == STATE_SPI_WRITE_SECTORS);

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
    case ATA_NOP:
      // Setting "abort" in the error register
      // Setting "error" in the status register
      // Clearing BUSY in the status register
      // Asserting the INTRQ signal
      TriggerEvent(EV_ATA_CMD_DONE);
      break;

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
    case SPI_REQ_STAT: {
      int addr = data[2];
      int sz = data[4];
      uint8_t stat[10];
      stat[0] = sectnum_.status;
      stat[1] = sectnum_.format << 4;
      stat[2] = 0x4;
      stat[3] = 2;
      stat[4] = 0;
      stat[5] = 0;
      stat[6] = 0;
      stat[7] = 0;
      stat[8] = 0;
      stat[9] = 0;
      TriggerEvent(EV_SPI_WRITE_START, reinterpret_cast<intptr_t>(&stat[addr]),
                   sz);
    } break;

    case SPI_REQ_MODE: {
      int addr = data[2];
      int sz = data[4];
      TriggerEvent(EV_SPI_WRITE_START, (intptr_t)&reply_11[addr >> 1], sz);
    } break;

    // case SPI_REQ_ERROR:
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
      bool msf = (data[1] & 0x1);

      cdreq_.dma = features_.dma;
      cdreq_.sector_format = (SectorFormat)((data[1] & 0xe) >> 1);
      cdreq_.sector_mask = (SectorMask)((data[1] >> 4) & 0xff);
      cdreq_.first_sector = GetFAD(data[2], data[3], data[4], msf);
      cdreq_.num_sectors = (data[8] << 16) | (data[9] << 8) | data[10];

      CHECK_EQ(cdreq_.sector_format, SECTOR_M1);

      TriggerEvent(EV_SPI_WRITE_SECTORS);
    } break;

    // case SPI_CD_READ2:
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

    case SPI_CD_OPEN:
    case SPI_CD_PLAY:
    case SPI_CD_SEEK:
    case SPI_CD_SCAN:
      TriggerEvent(EV_SPI_CMD_DONE);
      break;

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

void GDROM::ProcessSetMode(int offset, uint8_t *data, int data_size) {
  memcpy(reinterpret_cast<uint8_t *>(&reply_11[offset >> 1]), data, data_size);

  TriggerEvent(EV_SPI_CMD_DONE);
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

int GDROM::GetFAD(uint8_t a, uint8_t b, uint8_t c, bool msf) {
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
}

int GDROM::ReadSectors(int fad, SectorFormat format, SectorMask mask,
                       int num_sectors, uint8_t *dst, int dst_size) {
  CHECK(current_disc_);

  int total = 0;
  char data[SECTOR_SIZE];

  LOG_INFO("ReadSectors %d -> %d", fad, fad + num_sectors);

  for (int i = 0; i < num_sectors; i++) {
    int r = current_disc_->ReadSector(fad, data);
    CHECK_EQ(r, 1);

    if (format == SECTOR_M1 && mask == MASK_DATA) {
      CHECK_LT(total + 2048, dst_size);
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

GDROM_R32_DELEGATE(GD_ALTSTAT_DEVCTRL) {
  // this register is the same as the status register, but it does not
  // clear DMA status information when it is accessed
  return status_.full;
}

GDROM_W32_DELEGATE(GD_ALTSTAT_DEVCTRL) {
  // LOG_INFO("GD_DEVCTRL 0x%x", (uint32_t)value);
}

GDROM_R32_DELEGATE(GD_DATA) {
  uint16_t v = re::load<uint16_t>(&pio_buffer_[pio_head_]);
  pio_head_ += 2;
  if (pio_head_ == pio_size_) {
    TriggerEvent(EV_SPI_WRITE_END);
  }
  return v;
}

GDROM_W32_DELEGATE(GD_DATA) {
  re::store(&pio_buffer_[pio_head_], static_cast<uint16_t>(reg.value & 0xffff));
  pio_head_ += 2;

  // check if we've finished reading a command / the remaining data
  if ((state_ == STATE_SPI_READ_CMD && pio_head_ == SPI_CMD_SIZE) ||
      (state_ == STATE_SPI_READ_DATA && pio_head_ == pio_size_)) {
    TriggerEvent(EV_SPI_READ_END);
  }
}

GDROM_R32_DELEGATE(GD_ERROR_FEATURES) {
  // LOG_INFO("GD_ERROR");
  return 0;
}

GDROM_W32_DELEGATE(GD_ERROR_FEATURES) { features_.full = reg.value; }

GDROM_R32_DELEGATE(GD_INTREASON_SECTCNT) { return intreason_.full; }

GDROM_W32_DELEGATE(GD_INTREASON_SECTCNT) {
  // LOG_INFO("GD_SECTCNT 0x%x", reg.value);
}

GDROM_R32_DELEGATE(GD_SECTNUM) { return sectnum_.full; }

GDROM_W32_DELEGATE(GD_SECTNUM) { sectnum_.full = reg.value; }

GDROM_R32_DELEGATE(GD_BYCTLLO) { return byte_count_.lo; }

GDROM_W32_DELEGATE(GD_BYCTLLO) { byte_count_.lo = reg.value; }

GDROM_R32_DELEGATE(GD_BYCTLHI) { return byte_count_.hi; }

GDROM_W32_DELEGATE(GD_BYCTLHI) { byte_count_.hi = reg.value; }

GDROM_R32_DELEGATE(GD_DRVSEL) {
  // LOG_INFO("GD_DRVSEL");
  return 0;
}

GDROM_W32_DELEGATE(GD_DRVSEL) {
  // LOG_INFO("GD_DRVSEL 0x%x", (uint32_t)reg.value);
}

GDROM_R32_DELEGATE(GD_STATUS_COMMAND) {
  holly_->UnrequestInterrupt(HOLLY_INTC_G1GDINT);
  return status_.full;
}

GDROM_W32_DELEGATE(GD_STATUS_COMMAND) {
  ProcessATACommand(static_cast<ATACommand>(reg.value));
}
