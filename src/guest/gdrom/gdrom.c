#include "guest/gdrom/gdrom.h"
#include "core/core.h"
#include "guest/dreamcast.h"
#include "guest/gdrom/gdrom_replies.inc"
#include "guest/gdrom/gdrom_types.h"
#include "guest/holly/holly.h"
#include "imgui.h"

#if 0
#define LOG_GDROM LOG_INFO
#else
#define LOG_GDROM(...)
#endif

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
  { &gdrom_ata_cmd, &gdrom_pio_write, &gdrom_spi_cmd, NULL,            NULL,            },
  { &gdrom_ata_cmd, &gdrom_pio_write, NULL,           NULL,            &gdrom_spi_data, },
  { &gdrom_ata_cmd, NULL,             NULL,           &gdrom_pio_read, NULL,            },
  { &gdrom_ata_cmd, NULL,             NULL,           NULL,            NULL,            },
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
  int cdr_secfmt;
  int cdr_secmask;
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
  struct holly *hl = gd->dc->holly;

  gd->ireason.IO = 1;
  gd->ireason.CoD = 1;
  gd->status.DRDY = 1;
  gd->status.BSY = 0;
  gd->status.DRQ = 0;

  holly_raise_interrupt(hl, HOLLY_INT_G1GDINT);

  gd->state = STATE_READ_ATA_CMD;
}

static void gdrom_spi_cdread(struct gdrom *gd) {
  struct holly *hl = gd->dc->holly;

  if (gd->cdr_dma) {
    int max_dma_sectors = sizeof(gd->dma_buffer) / DISC_MAX_SECTOR_SIZE;

    /* fill DMA buffer with as many sectors as possible */
    int num_sectors = MIN(gd->cdr_num_sectors, max_dma_sectors);
    int res = gdrom_read_sectors(gd, gd->cdr_first_sector, num_sectors,
                                 gd->cdr_secfmt, gd->cdr_secmask,
                                 gd->dma_buffer, sizeof(gd->dma_buffer));
    gd->dma_size = res;
    gd->dma_head = 0;

    /* update sector read state */
    gd->cdr_first_sector += num_sectors;
    gd->cdr_num_sectors -= num_sectors;

    /* gdrom state won't be updated until DMA transfer is completed */
    gd->state = STATE_WRITE_DMA_DATA;
  } else {
    int max_pio_sectors = sizeof(gd->pio_buffer) / DISC_MAX_SECTOR_SIZE;

    /* fill PIO buffer with as many sectors as possible */
    int num_sectors = MIN(gd->cdr_num_sectors, max_pio_sectors);
    int res = gdrom_read_sectors(gd, gd->cdr_first_sector, num_sectors,
                                 gd->cdr_secfmt, gd->cdr_secmask,
                                 gd->pio_buffer, sizeof(gd->pio_buffer));
    gd->pio_size = res;
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

    holly_raise_interrupt(hl, HOLLY_INT_G1GDINT);

    gd->state = STATE_WRITE_SPI_DATA;
  }
}

static void gdrom_spi_read(struct gdrom *gd, int offset, int size) {
  struct holly *hl = gd->dc->holly;

  gd->cdr_num_sectors = 0;

  gd->pio_head = 0;
  gd->pio_size = size;
  gd->pio_offset = offset;

  gd->byte_count.full = size;
  gd->ireason.IO = 1;
  gd->ireason.CoD = 0;
  gd->status.DRQ = 1;
  gd->status.BSY = 0;

  holly_raise_interrupt(hl, HOLLY_INT_G1GDINT);

  gd->state = STATE_READ_SPI_DATA;
}

static void gdrom_spi_write(struct gdrom *gd, void *data, int size) {
  struct holly *hl = gd->dc->holly;

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

  holly_raise_interrupt(hl, HOLLY_INT_G1GDINT);

  gd->state = STATE_WRITE_SPI_DATA;
}

static void gdrom_ata_end(struct gdrom *gd) {
  struct holly *hl = gd->dc->holly;

  gd->status.DRDY = 1;
  gd->status.BSY = 0;

  holly_raise_interrupt(hl, HOLLY_INT_G1GDINT);

  gd->state = STATE_READ_ATA_CMD;
}

static void gdrom_spi_data(struct gdrom *gd, int arg) {
  /* only used by SET_MODE */
  int offset = gd->pio_offset;
  uint8_t *data = gd->pio_buffer;
  int size = gd->pio_size;
  memcpy((uint8_t *)&gd->hw_info + offset, data, size);

  gdrom_spi_end(gd);
}

static void gdrom_pio_read(struct gdrom *gd, int arg) {
  if (gd->pio_head >= gd->pio_size) {
    if (gd->cdr_num_sectors) {
      gdrom_spi_cdread(gd);
    } else {
      gdrom_spi_end(gd);
    }
  }
}

static void gdrom_spi_cmd(struct gdrom *gd, int arg) {
  uint8_t *data = gd->pio_buffer;
  int cmd = data[0];

  LOG_GDROM("gdrom_spi_cmd 0x%x", cmd);

  gd->status.DRQ = 0;
  gd->status.BSY = 1;

  switch (cmd) {
    /*
     * packet command flow for pio data to host
     */
    case GD_SPI_REQ_STAT: {
      int off = data[2];
      int len = data[4];

      struct gd_status_info stat;
      gdrom_get_status(gd, &stat);

      /* bit  |  7  |  6  |  5  |  4  |  3  |  2  |  1  |  0
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
         9    |  0  |  0  |  0  |  0  |  0  |  0  |  0  |  0 */
      uint8_t out[10];
      out[0] = stat.status & 0xff;
      out[1] = ((stat.format & 0xf) << 4) | (stat.repeat & 0xf);
      out[2] = ((stat.address & 0xf) << 4) | (stat.control & 0xf);
      out[3] = stat.scd_track & 0xff;
      out[4] = stat.scd_index & 0xff;
      out[5] = (stat.fad & 0x00ff0000) >> 16;
      out[6] = (stat.fad & 0x0000ff00) >> 8;
      out[7] = (stat.fad & 0x000000ff);
      out[8] = stat.read_retry & 0xff;
      out[9] = 0;

      gdrom_spi_write(gd, out + off, len);
    } break;

    case GD_SPI_REQ_MODE: {
      int off = data[2];
      int len = data[4];

      gdrom_spi_write(gd, (uint8_t *)&gd->hw_info + off, len);
    } break;

    case GD_SPI_REQ_ERR: {
      int len = data[4];

      struct gd_error_info err;
      gdrom_get_error(gd, &err);

      /* bit  |  7  |  6  |  5  |  4  |  3  |  2  |  1  |  0
         byte |     |     |     |     |     |     |     |
         -----------------------------------------------------
         0    |  1  |  1  |  1  |  1  |  0  |  0  |  0  |  0
         -----------------------------------------------------
         1    |  0  |  0  |  0  |  0  |  0  |  0  |  0  |  0
         -----------------------------------------------------
         2    |  0  |  0  |  0  |  0  |  sense key
         -----------------------------------------------------
         3    |  0  |  0  |  0  |  0  |  0  |  0  |  0  |  0
         -----------------------------------------------------
         4-7  |  cmd specific information
         -----------------------------------------------------
         8    |  additional sense code
         -----------------------------------------------------
         9    |  additional sense code qualifier */
      uint8_t out[10];
      data[0] = 0xf0;
      data[1] = 0;
      data[2] = err.sense & 0xf;
      data[3] = 0;
      data[4] = (err.info & 0x000000ff);
      data[5] = (err.info & 0x0000ff00) >> 8;
      data[6] = (err.info & 0x00ff0000) >> 16;
      data[7] = (err.info & 0xff000000) >> 24;
      data[8] = err.asc & 0xff;
      data[9] = err.ascq & 0xff;

      gdrom_spi_write(gd, out, len);
    } break;

    case GD_SPI_GET_TOC: {
      int area = (data[1] & 0x1);
      int len = (data[3] << 8) | data[4];

      struct gd_toc_info toc;
      gdrom_get_toc(gd, area, &toc);

      /* bit   |  7  |  6  |  5  |  4  |  3  |  2  |  1  |  0
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
      uint8_t out[408];
      for (int i = 0; i < ARRAY_SIZE(toc.entries); i++) {
        struct gd_toc_entry *entry = &toc.entries[i];
        out[i * 4 + 0] = ((entry->ctrl & 0xf) << 4) | (entry->adr & 0xf);
        out[i * 4 + 1] = (entry->fad & 0x00ff0000) >> 16;
        out[i * 4 + 2] = (entry->fad & 0x0000ff00) >> 8;
        out[i * 4 + 3] = (entry->fad & 0x000000ff);
      }
      out[396] = ((toc.first.ctrl & 0xf) << 4) | (toc.first.adr & 0xf);
      out[397] = toc.first.fad & 0xff;
      out[398] = 0;
      out[399] = 0;
      out[400] = ((toc.last.ctrl & 0xf) << 4) | (toc.last.adr & 0xf);
      out[401] = toc.last.fad & 0xff;
      out[402] = 0;
      out[403] = 0;
      out[404] = ((toc.leadout.ctrl & 0xf) << 4) | (toc.leadout.adr & 0xf);
      out[405] = (toc.leadout.fad & 0x00ff0000) >> 16;
      out[406] = (toc.leadout.fad & 0x0000ff00) >> 8;
      out[407] = (toc.leadout.fad & 0x000000ff);

      gdrom_spi_write(gd, out, len);
    } break;

    case GD_SPI_REQ_SES: {
      int session = data[2];
      int size = data[4];

      struct gd_session_info ses;
      gdrom_get_session(gd, session, &ses);

      /* bit  |  7  |  6  |  5  |  4  |  3  |  2  |  1  |  0
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
      uint8_t out[6];
      out[0] = ses.status & 0xf;
      out[1] = 0;
      out[2] = ses.track & 0xff;
      out[3] = (ses.fad & 0x00ff0000) >> 16;
      out[4] = (ses.fad & 0x0000ff00) >> 8;
      out[5] = (ses.fad & 0x000000ff);

      gdrom_spi_write(gd, out, size);
    } break;

    case GD_SPI_GET_SCD: {
      int format = data[1] & 0xf;
      int size = (data[3] << 8) | data[4];

      uint8_t scd[GD_SPI_SCD_SIZE];
      gdrom_get_subcode(gd, format, scd, sizeof(scd));

      gdrom_spi_write(gd, scd, size);
    } break;

    case GD_SPI_CD_READ: {
      int msf = (data[1] & 0x1);

      gd->cdr_dma = gd->features.dma;
      gd->cdr_secfmt = (data[1] & 0xe) >> 1;
      gd->cdr_secmask = (data[1] >> 4) & 0xff;
      gd->cdr_first_sector = gdrom_get_fad(data[2], data[3], data[4], msf);
      gd->cdr_num_sectors = (data[8] << 16) | (data[9] << 8) | data[10];

      gdrom_spi_cdread(gd);
    } break;

    case GD_SPI_CD_READ2: {
      LOG_FATAL("GD_SPI_CD_READ2");
    } break;

    /*
     * packet command flow for pio data from host
     */
    case GD_SPI_SET_MODE: {
      int offset = data[2];
      int size = data[4];

      gdrom_spi_read(gd, offset, size);
    } break;

    /*
     * non-data command flow
     */
    case GD_SPI_TEST_UNIT: {
      gdrom_spi_end(gd);
    } break;

    case GD_SPI_CD_OPEN: {
      LOG_FATAL("GD_SPI_CD_OPEN");
    } break;

    case GD_SPI_CD_PLAY: {
      LOG_WARNING("ignoring GD_SPI_CD_PLAY");

      gd->sectnum.status = GD_STATUS_PAUSE;

      gdrom_spi_end(gd);
    } break;

    case GD_SPI_CD_SEEK: {
      int param_type = data[1] & 0xf;

      LOG_WARNING("ignoring GD_SPI_CD_SEEK");

      switch (param_type) {
        case GD_SEEK_FAD:
        case GD_SEEK_MSF:
        case GD_SEEK_PAUSE:
          gd->sectnum.status = GD_STATUS_PAUSE;
          break;
        case GD_SEEK_STOP:
          gd->sectnum.status = GD_STATUS_STANDBY;
          break;
      }

      gdrom_spi_end(gd);
    } break;

    case GD_SPI_CD_SCAN: {
      LOG_WARNING("ignoring GD_SPI_CD_SCAN");

      gd->sectnum.status = GD_STATUS_PAUSE;

      gdrom_spi_end(gd);
    } break;

    /* GD_SPI_CHK_SEC and GD_SPI_REQ_SEC are part of an undocumented security
       check that has yet to be fully reverse engineered. the check doesn't seem
       to have any side effects, a canned response is sent when the results are
       requested */
    case GD_SPI_CHK_SEC: {
      gdrom_spi_end(gd);
    } break;

    case GD_SPI_REQ_SEC: {
      gdrom_spi_write(gd, (uint8_t *)reply_71, sizeof(reply_71));
    } break;

    default:
      LOG_FATAL("unsupported SPI command %d", cmd);
      break;
  }
}

static void gdrom_pio_write(struct gdrom *gd, int arg) {
  if (gd->state == STATE_READ_ATA_DATA && gd->pio_head == GD_SPI_CMD_SIZE) {
    gdrom_event(gd, EVENT_SPI_CMD, 0);
  } else if (gd->state == STATE_READ_SPI_DATA && gd->pio_head == gd->pio_size) {
    gdrom_event(gd, EVENT_SPI_DATA, 0);
  }
}

static void gdrom_ata_cmd(struct gdrom *gd, int cmd) {
  int read_data = 0;

  LOG_GDROM("gdrom_ata_cmd 0x%x", cmd);

  gd->status.DRDY = 0;
  gd->status.BSY = 1;

  /* error bits represent the status of the most recent command, clear before
    processing a new one */
  gd->error.full = 0;
  gd->status.CHECK = 0;

  switch (cmd) {
    case GD_ATA_NOP: {
      /* terminates the current command */
      gd->error.ABRT = 1;
      gd->status.CHECK = 1;
    } break;

    case GD_ATA_SOFT_RESET: {
      gdrom_set_disc(gd, gd->disc);
    } break;

    case GD_ATA_EXEC_DIAG: {
      LOG_FATAL("GD_ATA_EXEC_DIAG");
    } break;

    case GD_ATA_PACKET_CMD: {
      read_data = 1;
    } break;

    case GD_ATA_IDENTIFY_DEV: {
      LOG_FATAL("GD_ATA_IDENTIFY_DEV");
    } break;

    case GD_ATA_SET_FEATURES: {
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

static int gdrom_init(struct device *dev) {
  struct gdrom *gd = (struct gdrom *)dev;

  /* set default hardware information */
  memset(&gd->hw_info, 0, sizeof(gd->hw_info));
  gd->hw_info.speed = 0x0;
  gd->hw_info.standby_hi = 0x00;
  gd->hw_info.standby_lo = 0xb4;
  gd->hw_info.read_flags = 0x19;
  gd->hw_info.read_retry = 0x08;
  strncpy_pad_spaces(gd->hw_info.drive_info, "SE",
                     sizeof(gd->hw_info.drive_info));
  strncpy_pad_spaces(gd->hw_info.system_version, "Rev 6.43",
                     sizeof(gd->hw_info.system_version));
  strncpy_pad_spaces(gd->hw_info.system_date, "990408",
                     sizeof(gd->hw_info.system_date));

  gdrom_set_disc(gd, NULL);

  return 1;
}

int gdrom_read_bytes(struct gdrom *gd, int fad, int len, uint8_t *dst,
                     int dst_size) {
  if (!gd->disc) {
    LOG_WARNING("gdrom_read_sectors failed, no disc");
    return 0;
  }

  return disc_read_bytes(gd->disc, fad, len, dst, dst_size);
}

int gdrom_read_sectors(struct gdrom *gd, int fad, int num_sectors, int fmt,
                       int mask, uint8_t *dst, int dst_size) {
  if (!gd->disc) {
    LOG_WARNING("gdrom_read_sectors failed, no disc");
    return 0;
  }

  LOG_GDROM("gdrom_read_sectors [%d, %d)", fad, fad + num_sectors);

  return disc_read_sectors(gd->disc, fad, num_sectors, fmt, mask, dst,
                           dst_size);
}

int gdrom_find_file(struct gdrom *gd, const char *filename, int *fad,
                    int *len) {
  CHECK_NOTNULL(gd->disc);

  return disc_find_file(gd->disc, filename, fad, len);
}

void gdrom_get_bootfile(struct gdrom *gd, int *fad, int *len) {
  CHECK_NOTNULL(gd->disc);

  int res = disc_find_file(gd->disc, gd->disc->bootnme, fad, len);
  CHECK(res);
}

void gdrom_get_subcode(struct gdrom *gd, int format, uint8_t *data, int size) {
  CHECK_NOTNULL(gd->disc);
  CHECK_GE(size, GD_SPI_SCD_SIZE);

  /* FIXME implement */
  memset(data, 0, GD_SPI_SCD_SIZE);
  data[1] = GD_AUDIO_NOSTATUS;

  switch (format) {
    case 0:
      data[2] = 0x0;
      data[3] = 0x64;
      break;
    case 1:
      data[2] = 0x0;
      data[3] = 0xe;
      break;
  }

  LOG_GDROM("gdrom_get_subcode not fully implemented");
}

void gdrom_get_session(struct gdrom *gd, int session_num,
                       struct gd_session_info *ses) {
  CHECK_NOTNULL(gd->disc);

  memset(ses, 0, sizeof(*ses));

  ses->status = gd->sectnum.status;

  /* when session is 0 the "track" field contains the total number of sessions,
     while the "fad" field contains the lead-out fad

     when session is non-0, the "track" field contains the first track of the
     session, while the "fad" field contains contains the starting fad of the
     specified session */
  if (session_num == 0) {
    int num_sessions = disc_get_num_sessions(gd->disc);
    struct session *last_session = disc_get_session(gd->disc, num_sessions - 1);
    ses->track = num_sessions;
    ses->fad = last_session->leadout_fad;
  } else {
    struct session *session = disc_get_session(gd->disc, session_num - 1);
    struct track *first_track = disc_get_track(gd->disc, session->first_track);
    ses->track = first_track->num;
    ses->fad = first_track->fad;
  }
}

void gdrom_get_toc(struct gdrom *gd, int area, struct gd_toc_info *toc) {
  CHECK_NOTNULL(gd->disc);

  struct track *first_track = NULL;
  struct track *last_track = NULL;
  int leadin_fad = 0;
  int leadout_fad = 0;
  disc_get_toc(gd->disc, area, &first_track, &last_track, &leadin_fad,
               &leadout_fad);

  /* 0xffffffff represents an invalid track */
  memset(toc, 0xff, sizeof(*toc));

  /* write out entries for each track */
  for (int i = first_track->num; i <= last_track->num; i++) {
    struct track *track = disc_get_track(gd->disc, i - 1);
    struct gd_toc_entry *entry = &toc->entries[i - 1];

    entry->adr = track->adr;
    entry->ctrl = track->ctrl;
    entry->fad = track->fad;
  }

  toc->first.adr = first_track->adr;
  toc->first.ctrl = first_track->ctrl;
  toc->first.fad = first_track->num;

  toc->last.adr = last_track->adr;
  toc->last.ctrl = last_track->ctrl;
  toc->last.fad = last_track->num;

  toc->leadout.fad = leadout_fad;
}

void gdrom_get_error(struct gdrom *gd, struct gd_error_info *err) {
  CHECK_NOTNULL(gd->disc);

  memset(err, 0, sizeof(*err));

  /* TODO implement the sense key / code information */
  err->one = 0xf;
  err->sense = gd->error.sense_key;

  CHECK_EQ(sizeof(*err), 10);
}

void gdrom_get_status(struct gdrom *gd, struct gd_status_info *stat) {
  CHECK_NOTNULL(gd->disc);

  memset(stat, 0, sizeof(*stat));

  stat->status = gd->sectnum.status;
  stat->repeat = 0;
  stat->format = gd->sectnum.format;
  stat->control = 0x4;
  stat->address = 0;
  stat->scd_track = 2;
  stat->scd_index = 0;
  stat->fad = 0x0;
}

void gdrom_set_mode(struct gdrom *gd, struct gd_hw_info *info) {
  gd->hw_info = *info;
}

void gdrom_get_mode(struct gdrom *gd, struct gd_hw_info *info) {
  *info = gd->hw_info;
}

int gdrom_is_busy(struct gdrom *gd) {
  return gd->status.BSY;
}

void gdrom_dma_end(struct gdrom *gd) {
  LOG_GDROM("gd_dma_end");
}

int gdrom_dma_read(struct gdrom *gd, uint8_t *data, int n) {
  /* read more if the current dma buffer has been completely exhausted */
  if (gd->dma_head >= gd->dma_size) {
    if (gd->cdr_num_sectors) {
      gdrom_spi_cdread(gd);
    } else {
      gdrom_spi_end(gd);
    }
  }

  int remaining = gd->dma_size - gd->dma_head;
  n = MIN(n, remaining);

  if (n) {
    LOG_GDROM("gdrom_dma_read %d / %d bytes", gd->dma_head + n, gd->dma_size);
    memcpy(data, &gd->dma_buffer[gd->dma_head], n);
    gd->dma_head += n;
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
  if (gd->disc) {
    gd->sectnum.status = GD_STATUS_PAUSE;
    gd->sectnum.format = disc_get_format(disc);
  } else {
    gd->sectnum.status = GD_STATUS_NODISC;
  }

  /* TODO how do GD_FEATURES, GD_INTREASON, GD_BYCTLLO and GD_BYCTLHI behave */
}

struct disc *gdrom_get_disc(struct gdrom *gd) {
  return gd->disc;
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
  LOG_GDROM("read GD_ERROR 0x%x", value);
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
  struct holly *hl = dc->holly;
  uint16_t value = gd->status.full;
  LOG_GDROM("read GD_STATUS 0x%x", value);
  holly_clear_interrupt(hl, HOLLY_INT_G1GDINT);
  return value;
}

REG_W32(holly_cb, GD_STATUS_COMMAND) {
  struct gdrom *gd = dc->gdrom;
  LOG_GDROM("write GD_COMMAND 0x%x", value);
  gdrom_event(gd, EVENT_ATA_CMD, value);
}
