#include "hw/gdrom/gdrom.h"
#include "core/math.h"
#include "core/string.h"
#include "hw/dreamcast.h"
#include "hw/gdrom/gdrom_replies.inc"
#include "hw/gdrom/gdrom_types.h"
#include "hw/holly/holly.h"

#if 0
#define LOG_GDROM LOG_INFO
#else
#define LOG_GDROM(...)
#endif

#define bswap24(fad) \
  (((fad & 0xff) << 16) | (fad & 0x00ff00) | ((fad & 0xff0000) >> 16))

/* internal gdrom state machine */
enum gd_event {
  EVENT_ATA_CMD,
  EVENT_PIO_WRITE,
  EVENT_SPI_CMD,
  EVENT_PIO_READ,
  EVENT_SPI_DATA,
  MAX_EVENTS,
};

enum gd_state {
  STATE_READ_ATA_CMD,
  STATE_READ_ATA_DATA,
  STATE_READ_SPI_DATA,
  STATE_WRITE_SPI_DATA,
  STATE_WRITE_DMA_DATA,
  MAX_STATES,
};

typedef void (*gd_event_cb)(struct gdrom *, int);

static void gdrom_event(struct gdrom *gd, enum gd_event ev, int arg);
static void gdrom_ata_cmd(struct gdrom *gd, int arg);
static void gdrom_pio_write(struct gdrom *gd, int arg);
static void gdrom_spi_cmd(struct gdrom *gd, int arg);
static void gdrom_pio_read(struct gdrom *gd, int arg);
static void gdrom_spi_data(struct gdrom *gd, int arg);

/* clang-format off */
gd_event_cb gd_transitions[MAX_STATES][MAX_EVENTS] = {
  { &gdrom_ata_cmd, NULL,             NULL,           NULL,            NULL,            },
  { NULL,           &gdrom_pio_write, &gdrom_spi_cmd, NULL,            NULL,            },
  { NULL,           &gdrom_pio_write, NULL,           NULL,            &gdrom_spi_data, },
  { NULL,           NULL,             NULL,           &gdrom_pio_read, NULL,            },
  { NULL,           NULL,             NULL,           &gdrom_pio_read, NULL,            },
};
/* clang-format on */

struct gdrom {
  struct device;

  enum gd_state state;
  struct gd_hw_info hw_info;
  struct disc *disc;

  /* internal registers */
  union gd_error error;
  union gd_features features;
  union gd_intreason ireason;
  union gd_sectnum sectnum;
  union gd_bytect byte_count;
  union gd_status status;

  /* cdread state */
  int cdr_dma;
  enum gd_secfmt cdr_secfmt;
  enum gd_secmask cdr_secmask;
  int cdr_first_sector;
  int cdr_num_sectors;

  /* pio state */
  uint8_t pio_buffer[0x10000];
  int pio_head;
  int pio_size;
  int pio_offset;

  /* dma state */
  uint8_t dma_buffer[0x10000];
  int dma_head;
  int dma_size;
};

static int gdrom_get_fad(uint8_t a, uint8_t b, uint8_t c, int msf) {
  if (msf) {
    /* MSF mode
       Byte 2 - Start time: minutes (binary 0 - 255)
       Byte 3 - Start time: seconds (binary 0 - 59)
       Byte 4 - Start time: frames (binary 0 - 74) */
    return (a * 60 * 75) + (b * 75) + c;
  }

  /* FAD mode
     Byte 2 - Start frame address (MSB)
     Byte 3 - Start frame address
     Byte 4 - Start frame address (LSB) */
  return (a << 16) | (b << 8) | c;
}

static void gdrom_spi_end(struct gdrom *gd) {
  gd->ireason.IO = 1;
  gd->ireason.CoD = 1;
  gd->status.DRDY = 1;
  gd->status.BSY = 0;
  gd->status.DRQ = 0;

  holly_raise_interrupt(gd->holly, HOLLY_INTC_G1GDINT);

  gd->state = STATE_READ_ATA_CMD;
}

static void gdrom_spi_cdread(struct gdrom *gd) {
  if (gd->cdr_dma) {
    int max_dma_sectors = sizeof(gd->dma_buffer) / SECTOR_SIZE;

    /* fill DMA buffer with as many sectors as possible */
    int num_sectors = MIN(gd->cdr_num_sectors, max_dma_sectors);
    gd->dma_size = gdrom_read_sectors(gd, gd->cdr_first_sector, gd->cdr_secfmt,
                                      gd->cdr_secmask, num_sectors,
                                      gd->dma_buffer, sizeof(gd->dma_buffer));
    gd->dma_head = 0;

    /* update sector read state */
    gd->cdr_first_sector += num_sectors;
    gd->cdr_num_sectors -= num_sectors;

    /* gdrom state won't be updated until DMA transfer is completed */
    gd->state = STATE_WRITE_DMA_DATA;
  } else {
    int max_pio_sectors = sizeof(gd->pio_buffer) / SECTOR_SIZE;

    /* fill PIO buffer with as many sectors as possible */
    int num_sectors = MIN(gd->cdr_num_sectors, max_pio_sectors);
    gd->pio_size = gdrom_read_sectors(
        gd, gd->cdr_first_sector, gd->cdr_secfmt, gd->cdr_secmask, num_sectors,
        gd->pio_buffer, (int)sizeof(gd->pio_buffer));
    gd->pio_head = 0;

    /* update sector read state */
    gd->cdr_first_sector += num_sectors;
    gd->cdr_num_sectors -= num_sectors;

    /* update gdrom state */
    gd->byte_count.full = gd->pio_size;
    gd->ireason.IO = 1;
    gd->ireason.CoD = 0;
    gd->status.DRQ = 1;
    gd->status.BSY = 0;

    holly_raise_interrupt(gd->holly, HOLLY_INTC_G1GDINT);

    gd->state = STATE_WRITE_SPI_DATA;
  }
}

static void gdrom_spi_read(struct gdrom *gd, int offset, int size) {
  gd->cdr_num_sectors = 0;

  gd->pio_head = 0;
  gd->pio_size = size;
  gd->pio_offset = offset;

  gd->byte_count.full = size;
  gd->ireason.IO = 1;
  gd->ireason.CoD = 0;
  gd->status.DRQ = 1;
  gd->status.BSY = 0;

  holly_raise_interrupt(gd->holly, HOLLY_INTC_G1GDINT);

  gd->state = STATE_READ_SPI_DATA;
}

static void gdrom_spi_write(struct gdrom *gd, void *data, int size) {
  gd->cdr_num_sectors = 0;

  CHECK(size < (int)sizeof(gd->pio_buffer));
  memcpy(gd->pio_buffer, data, size);
  gd->pio_size = size;
  gd->pio_head = 0;

  gd->byte_count.full = gd->pio_size;
  gd->ireason.IO = 1;
  gd->ireason.CoD = 0;
  gd->status.DRQ = 1;
  gd->status.BSY = 0;

  holly_raise_interrupt(gd->holly, HOLLY_INTC_G1GDINT);

  gd->state = STATE_WRITE_SPI_DATA;
}

static void gdrom_ata_end(struct gdrom *gd) {
  gd->status.DRDY = 1;
  gd->status.BSY = 0;

  holly_raise_interrupt(gd->holly, HOLLY_INTC_G1GDINT);

  gd->state = STATE_READ_ATA_CMD;
}

static void gdrom_spi_data(struct gdrom *gd, int arg) {
  /* only used by SET_MODE */
  int offset = gd->pio_offset;
  uint8_t *data = gd->pio_buffer;
  int size = gd->pio_size;
  memcpy((void *)&gd->hw_info + offset, data, size);

  gdrom_spi_end(gd);
}

static void gdrom_pio_read(struct gdrom *gd, int arg) {
  if (gd->pio_head == gd->pio_size) {
    if (gd->cdr_num_sectors) {
      gdrom_spi_cdread(gd);
    } else {
      gdrom_spi_end(gd);
    }
  }
}

static void gdrom_spi_cmd(struct gdrom *gd, int arg) {
  uint8_t *data = gd->pio_buffer;
  enum gd_spi_cmd cmd = (enum gd_spi_cmd)data[0];

  LOG_GDROM("gdrom_spi_cmd 0x%x", cmd);

  gd->status.DRQ = 0;
  gd->status.BSY = 1;

  switch (cmd) {
    /*
     * packet command flow for pio data to host
     */
    case SPI_REQ_STAT: {
      int offset = data[2];
      int size = data[4];

      uint8_t stat[SPI_STAT_SIZE];
      gdrom_get_status(gd, stat, (int)sizeof(stat));

      gdrom_spi_write(gd, stat + offset, size);
    } break;

    case SPI_REQ_MODE: {
      int offset = data[2];
      int size = data[4];

      gdrom_spi_write(gd, (void *)&gd->hw_info + offset, size);
    } break;

    /*case SPI_REQ_ERROR: {
    } break;*/

    case SPI_GET_TOC: {
      enum gd_area area_type = (enum gd_area)(data[1] & 0x1);
      int size = (data[3] << 8) | data[4];

      uint8_t toc[SPI_TOC_SIZE];
      gdrom_get_toc(gd, area_type, toc, (int)sizeof(toc));

      gdrom_spi_write(gd, toc, size);
    } break;

    case SPI_REQ_SES: {
      int session = data[2];
      int size = data[4];

      uint8_t ses[SPI_SES_SIZE];
      gdrom_get_session(gd, session, ses, (int)sizeof(ses));

      gdrom_spi_write(gd, ses, (int)sizeof(ses));
    } break;

    case SPI_GET_SCD: {
      int format = data[1] & 0xf;
      int size = (data[3] << 8) | data[4];

      uint8_t scd[SPI_SCD_SIZE];
      gdrom_get_subcode(gd, format, scd);

      gdrom_spi_write(gd, scd, size);
    } break;

    case SPI_CD_READ: {
      int msf = (data[1] & 0x1);

      gd->cdr_dma = gd->features.dma;
      gd->cdr_secfmt = (enum gd_secfmt)((data[1] & 0xe) >> 1);
      gd->cdr_secmask = (enum gd_secmask)((data[1] >> 4) & 0xff);
      gd->cdr_first_sector = gdrom_get_fad(data[2], data[3], data[4], msf);
      gd->cdr_num_sectors = (data[8] << 16) | (data[9] << 8) | data[10];

      gdrom_spi_cdread(gd);
    } break;

    /*case SPI_CD_READ2: {
    } break;*/

    /*
     * packet command flow for pio data from host
     */
    case SPI_SET_MODE: {
      int offset = data[2];
      int size = data[4];

      gdrom_spi_read(gd, offset, size);
    } break;

    /*
     * non-data command flow
     */
    case SPI_TEST_UNIT: {
      gdrom_spi_end(gd);
    } break;

    case SPI_CD_OPEN:
    case SPI_CD_PLAY:
    case SPI_CD_SEEK:
    case SPI_CD_SCAN: {
      gdrom_spi_end(gd);
    } break;

    /* SPI_CHK_SECU / SPI_REQ_SECU are part of an undocumented security check
       that has yet to be fully reverse engineered. the check doesn't seem to
       have any side effects other than setting the drive to the PAUSE state,
       and a valid, canned response is sent when the results are requested */
    case SPI_CHK_SECU: {
      gd->sectnum.status = DST_PAUSE;

      gdrom_spi_end(gd);
    } break;

    case SPI_REQ_SECU: {
      gdrom_spi_write(gd, (uint8_t *)reply_71, sizeof(reply_71));
    } break;

    default:
      LOG_FATAL("unsupported SPI command %d", cmd);
      break;
  }
}

static void gdrom_pio_write(struct gdrom *gd, int arg) {
  if (gd->state == STATE_READ_ATA_DATA && gd->pio_head == SPI_CMD_SIZE) {
    gdrom_event(gd, EVENT_SPI_CMD, 0);
  } else if (gd->state == STATE_READ_SPI_DATA && gd->pio_head == gd->pio_size) {
    gdrom_event(gd, EVENT_SPI_DATA, 0);
  }
}

static void gdrom_ata_cmd(struct gdrom *gd, int arg) {
  enum gd_ata_cmd cmd = (enum gd_ata_cmd)arg;
  int read_data = 0;

  LOG_GDROM("gdrom_ata_cmd 0x%x", cmd);

  gd->error.ABRT = 0;
  gd->status.DRDY = 0;
  gd->status.BSY = 1;

  switch (cmd) {
    case ATA_NOP: {
      gd->error.ABRT = 1;
    } break;

    case ATA_SOFT_RESET: {
      gdrom_set_disc(gd, gd->disc);
    } break;

    /*case ATA_EXEC_DIAG: {
      LOG_FATAL("unhandled");
    } break;*/

    case ATA_PACKET_CMD: {
      read_data = 1;
    } break;

    /*case ATA_IDENTIFY_DEV: {
      LOG_FATAL("unhandled");
    } break;*/

    case ATA_SET_FEATURES: {
      /* transfer mode settings are ignored */
    } break;

    default:
      LOG_FATAL("unsupported ATA command %d", cmd);
      break;
  }

  if (read_data) {
    gd->pio_head = 0;

    gd->ireason.CoD = 1;
    gd->ireason.IO = 0;
    gd->status.DRQ = 1;
    gd->status.BSY = 0;

    gd->state = STATE_READ_ATA_DATA;
  } else {
    gdrom_ata_end(gd);
  }
}

static void gdrom_event(struct gdrom *gd, enum gd_event ev, int arg) {
  gd_event_cb cb = gd_transitions[gd->state][ev];
  CHECK(cb);
  cb(gd, arg);
}

int gdrom_read_sectors(struct gdrom *gd, int fad, enum gd_secfmt fmt,
                       enum gd_secmask mask, int num_sectors, uint8_t *dst,
                       int dst_size) {
  CHECK(gd->disc);

  int total = 0;
  char data[SECTOR_SIZE];

  LOG_GDROM("gdrom_read_sectors [%d, %d)", fad, fad + num_sectors);

  for (int i = 0; i < num_sectors; i++) {
    int r = disc_read_sector(gd->disc, fad, data);
    CHECK_EQ(r, 1);

    if (fmt == SECTOR_M1 && mask == MASK_DATA) {
      CHECK_LE(total + 2048, dst_size);
      memcpy(dst, data + 16, 2048);
      dst += 2048;
      total += 2048;
      fad++;
    } else {
      LOG_FATAL("unsupported sector format");
    }
  }

  return total;
}

void gdrom_get_subcode(struct gdrom *gd, int format, uint8_t *data) {
  CHECK(gd->disc);

  /* FIXME implement */
  memset(data, 0, SPI_SCD_SIZE);
  data[1] = AST_NOSTATUS;

  LOG_GDROM("gdrom_get_subcode not fully implemented");
}

void gdrom_get_session(struct gdrom *gd, int session, uint8_t *data, int size) {
  CHECK_GE(size, SPI_SES_SIZE);

  CHECK(gd->disc);

  /* session response layout:

     bit  |  7  |  6  |  5  |  4  |  3  |  2  |  1  |  0
     byte |     |     |     |     |     |     |     |
     -----------------------------------------------------
     0    |  0  |  0  |  0  |  0  |  status
     -----------------------------------------------------
     1    |  0  |  0  |  0  |  0  |  0  |  0  |  0  |  0
     -----------------------------------------------------
     2    |  number of sessions / starting track
     -----------------------------------------------------
     3    |  lead out fad (msb) / starting fad (msb)
     -----------------------------------------------------
     4    |  lead out fad / starting fad
     -----------------------------------------------------
     5    |  lead out fad (lsb) / starting fad (lsb) */

  uint8_t status = gd->sectnum.status;
  uint8_t track = 0;
  uint32_t fad = 0;

  if (session == 0) {
    /* when session is 0, the "fad" field contains the lead-out fad, while the
       "track" field contains the total number of sessions */
    if (1 /* is gd-rom */) {
      track = 2;
      fad = 0x861b4;
    }
  } else {
    /* when session is non-0, the "fad" field contains contains the starting fad
       of the specified session, while the "track" field contains the first
       track of the session */
    if (session == 1) {
      struct track *trk = disc_get_track(gd->disc, 0);
      track = 1;
      fad = trk->fad;
    } else if (session == 2) {
      struct track *trk = disc_get_track(gd->disc, 2);
      track = 3;
      fad = trk->fad;
    }
  }

  /* fad is written out big-endian */
  fad = bswap24(fad);

  data[0] = status;
  data[1] = 0;
  memcpy(&data[2], &fad, 3);
  data[5] = track;
}

void gdrom_get_toc(struct gdrom *gd, enum gd_area area_type, uint8_t *data,
                   int size) {
  CHECK_GE(size, SPI_TOC_SIZE);
  CHECK(gd->disc);

  /* toc response layout:

     bit   |  7  |  6  |  5  |  4  |  3  |  2  |  1  |  0
     byte  |     |     |     |     |     |     |     |
     ------------------------------------------------------
     n*4+0 | track n control       | track n adr
     a-----------------------------------------------------
     n*4*1 | track n fad (msb)
     ------------------------------------------------------
     n*4+2 | track n fad
     ------------------------------------------------------
     n*4+3 | track n fad (lsb)
     ------------------------------------------------------
     396   | start track control   | start track adr
     ------------------------------------------------------
     397   | start track number
     ------------------------------------------------------
     398   |  0  |  0  |  0  |  0  |  0  |  0  |  0  |  0
     ------------------------------------------------------
     399   |  0  |  0  |  0  |  0  |  0  |  0  |  0  |  0
     ------------------------------------------------------
     400   | end track control     | end track adr
     ------------------------------------------------------
     401   | end track number
     ------------------------------------------------------
     402   |  0  |  0  |  0  |  0  |  0  |  0  |  0  |  0
     ------------------------------------------------------
     403   |  0  |  0  |  0  |  0  |  0  |  0  |  0  |  0
     ------------------------------------------------------
     404   | lead-out track ctrl   | lead-out track adr
     ------------------------------------------------------
     405   | lead-out track fad (msb)
     ------------------------------------------------------
     406   | lead-out track fad
     ------------------------------------------------------
     407   | lead-out track fad (lsb) */

  /* for GD-ROMs, the single density area contains tracks 1 and 2, while the
     dual density area contains tracks 3 - num_tracks */
  int start_track_num = 0;
  int end_track_num = disc_get_num_tracks(gd->disc) - 1;

  /* TODO conditionally check disc to make sure it's a GD-ROM once
     CD-ROMs are supported */
  if (1 /* is gd-rom */) {
    if (area_type == AREA_SINGLE) {
      end_track_num = 1;
    } else {
      start_track_num = 2;
    }
  }

  uint32_t *entry = (uint32_t *)data;
  const struct track *start = disc_get_track(gd->disc, start_track_num);
  const struct track *end = disc_get_track(gd->disc, end_track_num);
  int leadout_fad = area_type == AREA_SINGLE ? 0x4650 : 0x861b4;

  memset(data, 0, SPI_TOC_SIZE);

  /* write out entries for each track */
  for (int i = start_track_num; i <= end_track_num; i++) {
    const struct track *track = disc_get_track(gd->disc, i);
    *(entry++) = (bswap24(track->fad) << 8) | (track->ctrl << 4) | track->adr;
  }

  /* write out start, end and lead-out track */
  *(entry++) = (start_track_num << 8) | (start->ctrl << 4) | start->adr;
  *(entry++) = (end_track_num << 8) | (end->ctrl << 4) | end->adr;
  *(entry++) = (bswap24(leadout_fad) << 8);
}

void gdrom_get_status(struct gdrom *gd, uint8_t *data, int size) {
  CHECK_GE(size, SPI_STAT_SIZE);

  /* cd status information response layout:

     bit  |  7  |  6  |  5  |  4  |  3  |  2  |  1  |  0
     byte |     |     |     |     |     |     |     |
     -----------------------------------------------------
     0    |  0  |  0  |  0  |  0  |  status
     -----------------------------------------------------
     1    |  disc format          |  repeat count
     -----------------------------------------------------
     2    |  address              |  control
     -----------------------------------------------------
     3    |  subcode q track number
     -----------------------------------------------------
     4    |  subcode q index number
     -----------------------------------------------------
     5    |  fad (msb)
     -----------------------------------------------------
     6    |  fad
     -----------------------------------------------------
     7    |  fad (lsb)
     -----------------------------------------------------
     8    |  max read retry time
     -----------------------------------------------------
     9   |  0  |  0  |  0  |  0  |  0  |  0  |  0  |  0 */

  uint32_t fad = bswap24(0x0);

  data[0] = gd->sectnum.status;
  data[1] = (gd->sectnum.format << 4) | 0;
  data[2] = 0x4;
  data[3] = 2;
  data[4] = 0;
  memcpy(&data[5], &fad, 3);
  data[8] = 0;
  data[9] = 0;
}

static int gdrom_init(struct device *dev) {
  struct gdrom *gd = (struct gdrom *)dev;

  /* set default hardware information */
  memset(&gd->hw_info, 0, sizeof(gd->hw_info));
  gd->hw_info.speed = 0x0;
  gd->hw_info.standby_hi = 0x00;
  gd->hw_info.standby_lo = 0xb4;
  gd->hw_info.read_flags = 0x19;
  gd->hw_info.read_retry = 0x08;
  strncpy_spaces(gd->hw_info.drive_info, "SE", sizeof(gd->hw_info.drive_info));
  strncpy_spaces(gd->hw_info.system_version, "Rev 6.43",
                 sizeof(gd->hw_info.system_version));
  strncpy_spaces(gd->hw_info.system_date, "990408",
                 sizeof(gd->hw_info.system_date));

  gdrom_set_disc(gd, NULL);

  return 1;
}

void gdrom_set_drive_mode(struct gdrom *gd, struct gd_hw_info *info) {
  gd->hw_info = *info;
}

void gdrom_get_drive_mode(struct gdrom *gd, struct gd_hw_info *info) {
  *info = gd->hw_info;
}

void gdrom_dma_end(struct gdrom *gd) {}

int gdrom_dma_read(struct gdrom *gd, uint8_t *data, int n) {
  /* try to read more if the current dma buffer has been completely read */
  if (gd->dma_head >= gd->dma_size) {
    gdrom_spi_cdread(gd);
  }

  int remaining = gd->dma_size - gd->dma_head;
  n = MIN(n, remaining);
  CHECK_GT(n, 0);

  LOG_GDROM("gdrom_dma_read %d / %d bytes", gd->dma_head + n, gd->dma_size);
  memcpy(data, &gd->dma_buffer[gd->dma_head], n);
  gd->dma_head += n;

  if (gd->dma_head >= gd->dma_size) {
    LOG_GDROM("gdrom_dma cd_read complete");

    /* CD_READ command is now done */
    gdrom_spi_end(gd);
  }

  return n;
}

void gdrom_dma_begin(struct gdrom *gd) {
  CHECK(gd->dma_size);

  LOG_GDROM("gd_dma_begin");
}

void gdrom_set_disc(struct gdrom *gd, struct disc *disc) {
  if (gd->disc != disc) {
    if (gd->disc) {
      disc_destroy(gd->disc);
    }

    gd->disc = disc;
  }

  /* perform "soft reset" of internal state */
  gd->error.full = 0;

  gd->status.full = 0;
  gd->status.DRDY = 1;
  gd->status.BSY = 0;

  gd->sectnum.full = 0;
  gd->sectnum.status = gd->disc ? DST_STANDBY : DST_NODISC;
  gd->sectnum.format = DISC_GDROM;

  /* TODO how do GD_FEATURES, GD_INTREASON, GD_BYCTLLO and GD_BYCTLHI behave */
}

void gdrom_destroy(struct gdrom *gd) {
  if (gd->disc) {
    disc_destroy(gd->disc);
  }

  dc_destroy_device((struct device *)gd);
}

struct gdrom *gdrom_create(struct dreamcast *dc) {
  struct gdrom *gd =
      dc_create_device(dc, sizeof(struct gdrom), "gdrom", &gdrom_init, NULL);
  return gd;
}

REG_R32(holly_cb, GD_ALTSTAT_DEVCTRL) {
  struct gdrom *gd = dc->gdrom;
  /* this register is the same as the status register, but it does not
     clear DMA status information when it is accessed */
  uint16_t value = gd->status.full;
  LOG_GDROM("read GD_ALTSTAT 0x%x", value);
  return value;
}

REG_W32(holly_cb, GD_ALTSTAT_DEVCTRL) {
  LOG_GDROM("write GD_DEVCTRL 0x%x [unimplemented]", value);
}

REG_R32(holly_cb, GD_DATA) {
  struct gdrom *gd = dc->gdrom;
  uint16_t value = *(uint16_t *)&gd->pio_buffer[gd->pio_head];

  LOG_GDROM("read GD_DATA 0x%x", value);

  gd->pio_head += 2;

  gdrom_event(gd, EVENT_PIO_READ, 0);

  return value;
}

REG_W32(holly_cb, GD_DATA) {
  struct gdrom *gd = dc->gdrom;

  LOG_GDROM("write GD_DATA 0x%x", value);

  *(uint16_t *)&gd->pio_buffer[gd->pio_head] = (uint16_t)(value & 0xffff);
  gd->pio_head += 2;

  gdrom_event(gd, EVENT_PIO_WRITE, 0);
}

REG_R32(holly_cb, GD_ERROR_FEATURES) {
  struct gdrom *gd = dc->gdrom;
  uint16_t value = gd->error.full;
  LOG_FATAL("read GD_ERROR 0x%x", value);
  return value;
}

REG_W32(holly_cb, GD_ERROR_FEATURES) {
  struct gdrom *gd = dc->gdrom;
  LOG_GDROM("write GD_FEATURES 0x%x", value);
  gd->features.full = value;
}

REG_R32(holly_cb, GD_INTREASON) {
  struct gdrom *gd = dc->gdrom;
  uint16_t value = gd->ireason.full;
  LOG_GDROM("read GD_INTREASON 0x%x", value);
  return value;
}

REG_W32(holly_cb, GD_INTREASON) {
  LOG_FATAL("invalid write to GD_INTREASON");
}

REG_R32(holly_cb, GD_SECTNUM) {
  struct gdrom *gd = dc->gdrom;
  uint16_t value = gd->sectnum.full;
  LOG_GDROM("read GD_SECTNUM 0x%x", value);
  return value;
}

REG_W32(holly_cb, GD_SECTNUM) {
  LOG_FATAL("invalid write to GD_SECTNUM");
}

REG_R32(holly_cb, GD_BYCTLLO) {
  struct gdrom *gd = dc->gdrom;
  uint16_t value = gd->byte_count.lo;
  LOG_GDROM("read GD_BYCTLLO 0x%x", value);
  return value;
}

REG_W32(holly_cb, GD_BYCTLLO) {
  struct gdrom *gd = dc->gdrom;
  LOG_GDROM("write GD_BYCTLLO 0x%x", value);
  gd->byte_count.lo = value;
}

REG_R32(holly_cb, GD_BYCTLHI) {
  struct gdrom *gd = dc->gdrom;
  uint16_t value = gd->byte_count.hi;
  LOG_GDROM("read GD_BYCTLHI 0x%x", value);
  return value;
}

REG_W32(holly_cb, GD_BYCTLHI) {
  struct gdrom *gd = dc->gdrom;
  LOG_GDROM("write GD_BYCTLHI 0x%x", value);
  gd->byte_count.hi = value;
}

REG_R32(holly_cb, GD_DRVSEL) {
  uint16_t value = 0;
  LOG_GDROM("read GD_DRVSEL 0x%x [unimplemented]", value);
  return value;
}

REG_W32(holly_cb, GD_DRVSEL) {
  LOG_GDROM("write GD_DRVSEL 0x%x [unimplemented]", value);
}

REG_R32(holly_cb, GD_STATUS_COMMAND) {
  struct gdrom *gd = dc->gdrom;
  uint16_t value = gd->status.full;
  LOG_GDROM("read GD_STATUS_COMMAND 0x%x", value);
  holly_clear_interrupt(gd->holly, HOLLY_INTC_G1GDINT);
  return value;
}

REG_W32(holly_cb, GD_STATUS_COMMAND) {
  struct gdrom *gd = dc->gdrom;
  LOG_GDROM("write GD_STATUS_COMMAND 0x%x", value);
  gdrom_event(gd, EVENT_ATA_CMD, value);
}
