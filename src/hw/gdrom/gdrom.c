#include "core/math.h"
#include "core/string.h"
#include "hw/gdrom/gdrom.h"
#include "hw/gdrom/gdrom_replies.inc"
#include "hw/gdrom/gdrom_types.h"
#include "hw/holly/holly.h"
#include "hw/dreamcast.h"

#define SWAP_24(fad) \
  (((fad & 0xff) << 16) | (fad & 0x00ff00) | ((fad & 0xff0000) >> 16))

static const int SPI_CMD_SIZE = 12;
static const int SUBCODE_SIZE = 100;

// internal gdrom state machine
typedef enum {
  EV_ATA_CMD_DONE,
  // each incomming SPI command will either:
  // a.) have no additional data and immediately fire EV_SPI_CMD_DONE
  // b.) read additional data over PIO with EV_SPI_READ_START
  // c.) write additional data over PIO with EV_SPI_WRITE_START
  // d.) write additional data over DMA / PIO with EV_SPI_WRITE_SECTORS
  EV_SPI_WAIT_CMD,
  EV_SPI_READ_START,
  EV_SPI_READ_END,
  EV_SPI_WRITE_START,
  EV_SPI_WRITE_SECTORS,
  EV_SPI_WRITE_END,
  EV_SPI_CMD_DONE,
} gd_event_t;

typedef enum {
  STATE_STANDBY,
  STATE_SPI_READ_CMD,
  STATE_SPI_READ_DATA,
  STATE_SPI_WRITE_DATA,
  STATE_SPI_WRITE_SECTORS,
} gd_state_t;

typedef struct {
  bool dma;
  gd_secfmt_t sector_fmt;
  gd_secmask_t sector_mask;
  int first_sector;
  int num_sectors;
} gd_cdread_t;

typedef struct gdrom_s {
  device_t base;

  holly_t *holly;

  gd_state_t state;
  struct disc_s *disc;
  gd_features_t features;
  gd_intreason_t ireason;
  gd_sectnum_t sectnum;
  gd_bytect_t byte_count;
  gd_status_t status;

  gd_cdread_t req;

  uint8_t pio_buffer[0x10000];
  int pio_head;
  int pio_size;
  int pio_read;

  uint8_t *dma_buffer;
  int dma_capacity;
  int dma_head;
  int dma_size;
} gdrom_t;

static bool gdrom_init(gdrom_t *gd);
static void gdrom_event(gdrom_t *gd, gd_event_t ev, intptr_t arg0,
                        intptr_t arg1);
static void gdrom_ata_cmd(gdrom_t *gd, gd_ata_cmd_t cmd);
static void gdrom_spi_cmd(gdrom_t *gd, uint8_t *data);
static void gdrom_set_mode(gdrom_t *gd, int offset, uint8_t *data,
                           int data_size);
static void gdrom_get_toc(gdrom_t *gd, gd_area_t area_type, gd_toc_t *toc);
static void gdrom_get_session(gdrom_t *gd, int session, gd_session_t *ses);
static void gdrom_get_subcode(gdrom_t *gd, int format, uint8_t *data);
static int gdrom_get_fad(uint8_t a, uint8_t b, uint8_t c, bool msf);
static int gdrom_read_sectors(gdrom_t *gd, int fad, gd_secfmt_t fmt,
                              gd_secmask_t mask, int num_sectors, uint8_t *dst,
                              int dst_size);
DECLARE_REG_R32(gdrom_t *gd, GD_ALTSTAT_DEVCTRL);
DECLARE_REG_W32(gdrom_t *gd, GD_ALTSTAT_DEVCTRL);
DECLARE_REG_R32(gdrom_t *gd, GD_DATA);
DECLARE_REG_W32(gdrom_t *gd, GD_DATA);
DECLARE_REG_R32(gdrom_t *gd, GD_ERROR_FEATURES);
DECLARE_REG_W32(gdrom_t *gd, GD_ERROR_FEATURES);
DECLARE_REG_R32(gdrom_t *gd, GD_INTREASON_SECTCNT);
DECLARE_REG_W32(gdrom_t *gd, GD_INTREASON_SECTCNT);
DECLARE_REG_R32(gdrom_t *gd, GD_SECTNUM);
DECLARE_REG_W32(gdrom_t *gd, GD_SECTNUM);
DECLARE_REG_R32(gdrom_t *gd, GD_BYCTLLO);
DECLARE_REG_W32(gdrom_t *gd, GD_BYCTLLO);
DECLARE_REG_R32(gdrom_t *gd, GD_BYCTLHI);
DECLARE_REG_W32(gdrom_t *gd, GD_BYCTLHI);
DECLARE_REG_R32(gdrom_t *gd, GD_DRVSEL);
DECLARE_REG_W32(gdrom_t *gd, GD_DRVSEL);
DECLARE_REG_R32(gdrom_t *gd, GD_STATUS_COMMAND);
DECLARE_REG_W32(gdrom_t *gd, GD_STATUS_COMMAND);

gdrom_t *gdrom_create(struct dreamcast_s *dc) {
  gdrom_t *gd = dc_create_device(dc, sizeof(gdrom_t), "gdrom",
                                 (device_init_cb)&gdrom_init);
  return gd;
}

void gdrom_destroy(gdrom_t *gd) {
  if (gd->disc) {
    disc_destroy(gd->disc);
  }

  dc_destroy_device(&gd->base);
}

bool gdrom_init(gdrom_t *gd) {
  gd->holly = gd->base.dc->holly;

// initialize registers
#define GDROM_REG_R32(name)       \
  gd->holly->reg_data[name] = gd; \
  gd->holly->reg_read[name] = (reg_read_cb)&name##_r;
#define GDROM_REG_W32(name)       \
  gd->holly->reg_data[name] = gd; \
  gd->holly->reg_write[name] = (reg_write_cb)&name##_w;
  GDROM_REG_R32(GD_ALTSTAT_DEVCTRL);
  GDROM_REG_W32(GD_ALTSTAT_DEVCTRL);
  GDROM_REG_R32(GD_DATA);
  GDROM_REG_W32(GD_DATA);
  GDROM_REG_R32(GD_ERROR_FEATURES);
  GDROM_REG_W32(GD_ERROR_FEATURES);
  GDROM_REG_R32(GD_INTREASON_SECTCNT);
  GDROM_REG_W32(GD_INTREASON_SECTCNT);
  GDROM_REG_R32(GD_SECTNUM);
  GDROM_REG_W32(GD_SECTNUM);
  GDROM_REG_R32(GD_BYCTLLO);
  GDROM_REG_W32(GD_BYCTLLO);
  GDROM_REG_R32(GD_BYCTLHI);
  GDROM_REG_W32(GD_BYCTLHI);
  GDROM_REG_R32(GD_DRVSEL);
  GDROM_REG_W32(GD_DRVSEL);
  GDROM_REG_R32(GD_STATUS_COMMAND);
  GDROM_REG_W32(GD_STATUS_COMMAND);
#undef GDROM_REG_R32
#undef GDROM_REG_W32

  gdrom_set_disc(gd, NULL);

  return true;
}

void gdrom_set_disc(gdrom_t *gd, struct disc_s *disc) {
  if (gd->disc != disc) {
    if (gd->disc) {
      disc_destroy(gd->disc);
    }

    gd->disc = disc;
  }

  // looking at "6.1.1 CD Drive State Transition Diagram" in CDIF131E.pdf, it
  // seems standby is the default state for when a disc is inserted
  gd->sectnum.status = gd->disc ? DST_STANDBY : DST_NODISC;
  gd->sectnum.format = DISC_GDROM;

  gd->status.full = 0;
  gd->status.DRDY = 1;
  gd->status.BSY = 0;
}

void gdrom_dma_begin(gdrom_t *gd) {}

int gdrom_dma_read(gdrom_t *gd, uint8_t *data, int data_size) {
  int remaining = gd->dma_size - gd->dma_head;
  int n = MIN(remaining, data_size);
  memcpy(data, &gd->dma_buffer[gd->dma_head], n);
  gd->dma_head += n;
  return n;
}

void gdrom_dma_end(gdrom_t *gd) {
  // reset DMA write state
  gd->dma_size = 0;

  // CD_READ command is now done
  gdrom_event(gd, EV_SPI_CMD_DONE, 0, 0);
}

void gdrom_event(gdrom_t *gd, gd_event_t ev, intptr_t arg0, intptr_t arg1) {
  switch (ev) {
    case EV_ATA_CMD_DONE: {
      CHECK_EQ(gd->state, STATE_STANDBY);

      gd->status.DRDY = 1;
      gd->status.BSY = 0;

      holly_raise_interrupt(gd->holly, HOLLY_INTC_G1GDINT);

      gd->state = STATE_STANDBY;
    } break;

    case EV_SPI_WAIT_CMD: {
      CHECK_EQ(gd->state, STATE_STANDBY);

      gd->pio_head = 0;

      gd->ireason.CoD = 1;
      gd->ireason.IO = 0;
      gd->status.DRQ = 1;
      gd->status.BSY = 0;

      gd->state = STATE_SPI_READ_CMD;
    } break;

    case EV_SPI_READ_START: {
      CHECK_EQ(gd->state, STATE_SPI_READ_CMD);

      int offset = (int)arg0;
      int size = (int)arg1;
      CHECK_NE(size, 0);

      gd->pio_head = 0;
      gd->pio_size = size;
      gd->pio_read = offset;

      gd->byte_count.full = size;
      gd->ireason.IO = 1;
      gd->ireason.CoD = 0;
      gd->status.DRQ = 1;
      gd->status.BSY = 0;

      holly_raise_interrupt(gd->holly, HOLLY_INTC_G1GDINT);

      gd->state = STATE_SPI_READ_DATA;
    } break;

    case EV_SPI_READ_END: {
      CHECK(gd->state == STATE_SPI_READ_CMD ||
            gd->state == STATE_SPI_READ_DATA);

      if (gd->state == STATE_SPI_READ_CMD) {
        CHECK_EQ(gd->pio_head, SPI_CMD_SIZE);
        gdrom_spi_cmd(gd, gd->pio_buffer);
      } else if (gd->state == STATE_SPI_READ_DATA) {
        gdrom_set_mode(gd, gd->pio_read, gd->pio_buffer, gd->pio_head);
      }
    } break;

    case EV_SPI_WRITE_START: {
      CHECK_EQ(gd->state, STATE_SPI_READ_CMD);

      uint8_t *data = (uint8_t *)arg0;
      int size = (int)arg1;

      CHECK(size && size < (int)sizeof(gd->pio_buffer));
      memcpy(gd->pio_buffer, data, size);
      gd->pio_size = size;
      gd->pio_head = 0;

      gd->byte_count.full = gd->pio_size;
      gd->ireason.IO = 1;
      gd->ireason.CoD = 0;
      gd->status.DRQ = 1;
      gd->status.BSY = 0;

      holly_raise_interrupt(gd->holly, HOLLY_INTC_G1GDINT);

      gd->state = STATE_SPI_WRITE_DATA;
    } break;

    case EV_SPI_WRITE_SECTORS: {
      CHECK(gd->state == STATE_SPI_READ_CMD ||
            gd->state == STATE_SPI_WRITE_SECTORS);

      if (gd->req.dma) {
        int max_dma_size = gd->req.num_sectors * SECTOR_SIZE;

        // reserve the worst case size
        if (max_dma_size > gd->dma_capacity) {
          gd->dma_buffer = realloc(gd->dma_buffer, max_dma_size);
          gd->dma_capacity = max_dma_size;
        }

        // read to DMA buffer
        gd->dma_size = gdrom_read_sectors(
            gd, gd->req.first_sector, gd->req.sector_fmt, gd->req.sector_mask,
            gd->req.num_sectors, gd->dma_buffer, gd->dma_capacity);
        gd->dma_head = 0;

        // gdrom state won't be updated until DMA transfer is completed
      } else {
        int max_pio_sectors = sizeof(gd->pio_buffer) / SECTOR_SIZE;

        // fill PIO buffer with as many sectors as possible
        int num_sectors = MIN(gd->req.num_sectors, max_pio_sectors);
        gd->pio_size = gdrom_read_sectors(
            gd, gd->req.first_sector, gd->req.sector_fmt, gd->req.sector_mask,
            num_sectors, gd->pio_buffer, (int)sizeof(gd->pio_buffer));
        gd->pio_head = 0;

        // update sector read state
        gd->req.first_sector += num_sectors;
        gd->req.num_sectors -= num_sectors;

        // update gdrom state
        gd->byte_count.full = gd->pio_size;
        gd->ireason.IO = 1;
        gd->ireason.CoD = 0;
        gd->status.DRQ = 1;
        gd->status.BSY = 0;

        holly_raise_interrupt(gd->holly, HOLLY_INTC_G1GDINT);
      }

      gd->state = STATE_SPI_WRITE_SECTORS;
    } break;

    case EV_SPI_WRITE_END: {
      CHECK(gd->state == STATE_SPI_WRITE_DATA ||
            gd->state == STATE_SPI_WRITE_SECTORS);

      // if there are still sectors remaining to be written out to the PIO
      // buffer, continue doing so
      if (gd->state == STATE_SPI_WRITE_SECTORS && gd->req.num_sectors) {
        gdrom_event(gd, EV_SPI_WRITE_SECTORS, 0, 0);
      } else {
        gdrom_event(gd, EV_SPI_CMD_DONE, 0, 0);
      }
    } break;

    case EV_SPI_CMD_DONE: {
      CHECK(gd->state == STATE_SPI_READ_CMD ||
            gd->state == STATE_SPI_READ_DATA ||
            gd->state == STATE_SPI_WRITE_DATA ||
            gd->state == STATE_SPI_WRITE_SECTORS);

      gd->ireason.IO = 1;
      gd->ireason.CoD = 1;
      gd->status.DRDY = 1;
      gd->status.BSY = 0;
      gd->status.DRQ = 0;

      holly_raise_interrupt(gd->holly, HOLLY_INTC_G1GDINT);

      gd->state = STATE_STANDBY;
    } break;
  }
}

void gdrom_ata_cmd(gdrom_t *gd, gd_ata_cmd_t cmd) {
  gd->status.DRDY = 0;
  gd->status.BSY = 1;

  switch (cmd) {
    case ATA_NOP:
      // Setting "abort" in the error register
      // Setting "error" in the status register
      // Clearing BUSY in the status register
      // Asserting the INTRQ signal
      gdrom_event(gd, EV_ATA_CMD_DONE, 0, 0);
      break;

    case ATA_SOFT_RESET:
      gdrom_set_disc(gd, gd->disc);
      gdrom_event(gd, EV_ATA_CMD_DONE, 0, 0);
      break;

    // case ATA_EXEC_DIAG:
    //   LOG_FATAL("Unhandled");
    //   break;

    case ATA_PACKET:
      gdrom_event(gd, EV_SPI_WAIT_CMD, 0, 0);
      break;

    // case ATA_IDENTIFY_DEV:
    //   LOG_FATAL("Unhandled");
    //   break;

    case ATA_SET_FEATURES:
      // FIXME I think we're supposed to be honoring GD_SECTCNT here to control
      // the DMA setting used by CD_READ
      gdrom_event(gd, EV_ATA_CMD_DONE, 0, 0);
      break;

    default:
      LOG_FATAL("Unsupported ATA command %d", cmd);
      break;
  }
}

void gdrom_spi_cmd(gdrom_t *gd, uint8_t *data) {
  gd_spi_cmd_t cmd = (gd_spi_cmd_t)data[0];

  gd->status.DRQ = 0;
  gd->status.BSY = 1;

  switch (cmd) {
    //
    // Packet Command Flow For PIO DATA To Host
    //
    case SPI_REQ_STAT: {
      int addr = data[2];
      int sz = data[4];
      uint8_t stat[10];
      stat[0] = gd->sectnum.status;
      stat[1] = gd->sectnum.format << 4;
      stat[2] = 0x4;
      stat[3] = 2;
      stat[4] = 0;
      stat[5] = 0;
      stat[6] = 0;
      stat[7] = 0;
      stat[8] = 0;
      stat[9] = 0;
      gdrom_event(gd, EV_SPI_WRITE_START, (intptr_t)&stat[addr], sz);
    } break;

    case SPI_REQ_MODE: {
      int addr = data[2];
      int sz = data[4];
      gdrom_event(gd, EV_SPI_WRITE_START, (intptr_t)&reply_11[addr >> 1], sz);
    } break;

    // case SPI_REQ_ERROR:
    //   break;

    case SPI_GET_TOC: {
      gd_area_t area_type = (gd_area_t)(data[1] & 0x1);
      int size = (data[3] << 8) | data[4];
      gd_toc_t toc;
      gdrom_get_toc(gd, area_type, &toc);
      gdrom_event(gd, EV_SPI_WRITE_START, (intptr_t)&toc, size);
    } break;

    case SPI_REQ_SES: {
      int session = data[2];
      int size = data[4];
      gd_session_t ses;
      gdrom_get_session(gd, session, &ses);
      gdrom_event(gd, EV_SPI_WRITE_START, (intptr_t)&ses, size);
    } break;

    case SPI_GET_SCD: {
      int format = data[1] & 0xffff;
      int size = (data[3] << 8) | data[4];
      uint8_t scd[SUBCODE_SIZE];
      gdrom_get_subcode(gd, format, scd);
      gdrom_event(gd, EV_SPI_WRITE_START, (intptr_t)scd, size);
    } break;

    case SPI_CD_READ: {
      bool msf = (data[1] & 0x1);

      gd->req.dma = gd->features.dma;
      gd->req.sector_fmt = (gd_secfmt_t)((data[1] & 0xe) >> 1);
      gd->req.sector_mask = (gd_secmask_t)((data[1] >> 4) & 0xff);
      gd->req.first_sector = gdrom_get_fad(data[2], data[3], data[4], msf);
      gd->req.num_sectors = (data[8] << 16) | (data[9] << 8) | data[10];

      CHECK_EQ(gd->req.sector_fmt, SECTOR_M1);

      gdrom_event(gd, EV_SPI_WRITE_SECTORS, 0, 0);
    } break;

    // case SPI_CD_READ2:
    //   break;

    //
    // Transfer Packet Command Flow For PIO Data from Host
    //
    case SPI_SET_MODE: {
      int offset = data[2];
      int size = data[4];
      gdrom_event(gd, EV_SPI_READ_START, offset, size);
    } break;

    //
    // Non-Data Command Flow
    //
    case SPI_TEST_UNIT:
      gdrom_event(gd, EV_SPI_CMD_DONE, 0, 0);
      break;

    case SPI_CD_OPEN:
    case SPI_CD_PLAY:
    case SPI_CD_SEEK:
    case SPI_CD_SCAN:
      gdrom_event(gd, EV_SPI_CMD_DONE, 0, 0);
      break;

    case SPI_UNKNOWN_70:
      gdrom_event(gd, EV_SPI_CMD_DONE, 0, 0);
      break;

    case SPI_UNKNOWN_71:
      gdrom_event(gd, EV_SPI_WRITE_START, (intptr_t)reply_71, sizeof(reply_71));
      break;

    default:
      LOG_FATAL("Unsupported SPI command %d", cmd);
      break;
  }
}

void gdrom_set_mode(gdrom_t *gd, int offset, uint8_t *data, int data_size) {
  memcpy((uint8_t *)&reply_11[offset >> 1], data, data_size);

  gdrom_event(gd, EV_SPI_CMD_DONE, 0, 0);
}

void gdrom_get_toc(gdrom_t *gd, gd_area_t area_type, gd_toc_t *toc) {
  CHECK(gd->disc);

  // for GD-ROMs, the single density area contains tracks 1 and 2, while the
  // dual density area contains tracks 3 - num_tracks
  int first_track_num = 0;
  int last_track_num = disc_num_tracks(gd->disc) - 1;

  // TODO conditionally check disc to make sure it's a GD-ROM once
  // CD-ROMs are supported
  if (1 /* is gd-rom */) {
    if (area_type == AREA_SINGLE) {
      last_track_num = 1;
    } else {
      first_track_num = 2;
    }
  }

  const track_t *start_track = disc_get_track(gd->disc, first_track_num);
  const track_t *end_track = disc_get_track(gd->disc, last_track_num);
  int leadout_fad = area_type == AREA_SINGLE ? 0x4650 : 0x861b4;

  memset(toc, 0, sizeof(*toc));
  for (int i = first_track_num; i <= last_track_num; i++) {
    gd_tocentry_t *entry = &toc->entries[i];
    const track_t *track = disc_get_track(gd->disc, i);
    entry->ctrl = track->ctrl;
    entry->adr = track->adr;
    entry->fad = track->num;
  }
  toc->start.ctrl = start_track->ctrl;
  toc->start.adr = start_track->adr;
  toc->start.fad = SWAP_24(end_track->fad);
  toc->end.ctrl = end_track->ctrl;
  toc->end.adr = end_track->adr;
  toc->end.fad = SWAP_24(end_track->fad);
  toc->leadout.ctrl = 0;
  toc->leadout.adr = 0;
  toc->leadout.fad = SWAP_24(leadout_fad);
}

void gdrom_get_session(gdrom_t *gd, int session, gd_session_t *ses) {
  CHECK(gd->disc);

  if (!session) {
    // session values have a different meaning for session == 0

    // TODO start_fad for non GD-Roms I guess is 0x4650
    if (1 /* is gd-rom */) {
      ses->first_track = 2;               // num sessions
      ses->start_fad = SWAP_24(0x861b4);  // end fad
    }
  } else if (session == 1) {
    ses->first_track = 1;
    ses->start_fad = SWAP_24(disc_get_track(gd->disc, 0)->fad);
  } else if (session == 2) {
    ses->first_track = 3;
    ses->start_fad = SWAP_24(disc_get_track(gd->disc, 2)->fad);
  }
}

void gdrom_get_subcode(gdrom_t *gd, int format, uint8_t *data) {
  CHECK(gd->disc);

  // FIXME implement
  memset(data, 0, SUBCODE_SIZE);
  data[1] = AST_NOSTATUS;

  LOG_INFO("GetSubcode not fully implemented");
}

int gdrom_get_fad(uint8_t a, uint8_t b, uint8_t c, bool msf) {
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

int gdrom_read_sectors(gdrom_t *gd, int fad, gd_secfmt_t fmt, gd_secmask_t mask,
                       int num_sectors, uint8_t *dst, int dst_size) {
  CHECK(gd->disc);

  int total = 0;
  char data[SECTOR_SIZE];

  LOG_INFO("ReadSectors %d -> %d", fad, fad + num_sectors);

  for (int i = 0; i < num_sectors; i++) {
    int r = disc_read_sector(gd->disc, fad, data);
    CHECK_EQ(r, 1);

    if (fmt == SECTOR_M1 && mask == MASK_DATA) {
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

REG_R32(gdrom_t *gd, GD_ALTSTAT_DEVCTRL) {
  // this register is the same as the status register, but it does not
  // clear DMA status information when it is accessed
  return gd->status.full;
}

REG_W32(gdrom_t *gd, GD_ALTSTAT_DEVCTRL) {
  // LOG_INFO("GD_DEVCTRL 0x%x", (uint32_t)value);
}

REG_R32(gdrom_t *gd, GD_DATA) {
  uint16_t v = *(uint16_t *)&gd->pio_buffer[gd->pio_head];
  gd->pio_head += 2;
  if (gd->pio_head == gd->pio_size) {
    gdrom_event(gd, EV_SPI_WRITE_END, 0, 0);
  }
  return v;
}

REG_W32(gdrom_t *gd, GD_DATA) {
  *(uint16_t *)&gd->pio_buffer[gd->pio_head] = (uint16_t)(*new_value & 0xffff);
  gd->pio_head += 2;

  // check if we've finished reading a command / the remaining data
  if ((gd->state == STATE_SPI_READ_CMD && gd->pio_head == SPI_CMD_SIZE) ||
      (gd->state == STATE_SPI_READ_DATA && gd->pio_head == gd->pio_size)) {
    gdrom_event(gd, EV_SPI_READ_END, 0, 0);
  }
}

REG_R32(gdrom_t *gd, GD_ERROR_FEATURES) {
  // LOG_INFO("GD_ERROR");
  return 0;
}

REG_W32(gdrom_t *gd, GD_ERROR_FEATURES) {
  gd->features.full = *new_value;
}

REG_R32(gdrom_t *gd, GD_INTREASON_SECTCNT) {
  return gd->ireason.full;
}

REG_W32(gdrom_t *gd, GD_INTREASON_SECTCNT) {
  // LOG_INFO("GD_SECTCNT 0x%x", *new_value);
}

REG_R32(gdrom_t *gd, GD_SECTNUM) {
  return gd->sectnum.full;
}

REG_W32(gdrom_t *gd, GD_SECTNUM) {
  gd->sectnum.full = *new_value;
}

REG_R32(gdrom_t *gd, GD_BYCTLLO) {
  return gd->byte_count.lo;
}

REG_W32(gdrom_t *gd, GD_BYCTLLO) {
  gd->byte_count.lo = *new_value;
}

REG_R32(gdrom_t *gd, GD_BYCTLHI) {
  return gd->byte_count.hi;
}

REG_W32(gdrom_t *gd, GD_BYCTLHI) {
  gd->byte_count.hi = *new_value;
}

REG_R32(gdrom_t *gd, GD_DRVSEL) {
  // LOG_INFO("GD_DRVSEL");
  return 0;
}

REG_W32(gdrom_t *gd, GD_DRVSEL) {
  // LOG_INFO("GD_DRVSEL 0x%x", (uint32_t)*new_value);
}

REG_R32(gdrom_t *gd, GD_STATUS_COMMAND) {
  holly_clear_interrupt(gd->holly, HOLLY_INTC_G1GDINT);
  return gd->status.full;
}

REG_W32(gdrom_t *gd, GD_STATUS_COMMAND) {
  gdrom_ata_cmd(gd, (gd_ata_cmd_t)*new_value);
}
