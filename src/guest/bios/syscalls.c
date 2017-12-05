#include "guest/bios/syscalls.h"
#include "guest/bios/bios.h"
#include "guest/bios/flash.h"
#include "guest/gdrom/gdrom.h"
#include "guest/holly/holly.h"
#include "guest/memory.h"
#include "guest/rom/flash.h"
#include "guest/sh4/sh4.h"

#if 0
#define LOG_SYSCALL LOG_INFO
#else
#define LOG_SYSCALL(...)
#endif

/*
 * system syscalls
 */
enum {
  SYSTEM_BOOT = -3,
  SYSTEM_UNKNOWN = -2,
  SYSTEM_RESET1 = -1,
  SYSTEM_SECURITY = 0,
  SYSTEM_RESET2 = 1,
  SYSTEM_CHKDISC = 2,
  SYSTEM_RESET3 = 3,
  SYSTEM_RESET4 = 4,
};

void bios_system_vector(struct bios *bios) {
  struct dreamcast *dc = bios->dc;
  struct sh4_context *ctx = &dc->sh4->ctx;

  uint32_t fn = ctx->r[4];

  LOG_SYSCALL("SYSTEM 0x%x", fn);

  /* nop, branch to the return address */
  ctx->pc = ctx->pr;

  switch (fn) {
    case SYSTEM_BOOT:
      bios_boot(bios);
      break;

    default:
      LOG_WARNING("bios_system_vector unhandled fn=0x%x", fn);
      break;
  }
}

/*
 * gdrom syscalls
 */
enum {
  MISC_INIT = 0x0,
  MISC_SETVECTOR = 0x1,
};

enum {
  GDROM_SEND_COMMAND = 0x0,
  GDROM_CHECK_COMMAND = 0x1,
  GDROM_MAINLOOP = 0x2,
  GDROM_INIT = 0x3,
  GDROM_CHECK_DRIVE = 0x4,
  GDROM_G1_DMA_END = 0x5,
  GDROM_REQ_DMA = 0x6,
  GDROM_CHECK_DMA = 0x7,
  GDROM_ABORT_COMMAND = 0x8,
  GDROM_RESET = 0x9,
  GDROM_SECTOR_MODE = 0xa,
};

enum {
  GDC_PIOREAD = 0x10,
  GDC_DMAREAD = 0x11,
  GDC_GETTOC = 0x12,
  GDC_GETTOC2 = 0x13,
  GDC_PLAY = 0x14,
  GDC_PLAY2 = 0x15,
  GDC_PAUSE = 0x16,
  GDC_RELEASE = 0x17,
  GDC_INIT = 0x18,
  GDC_SEEK = 0x1b,
  GDC_READ = 0x1c,
  GDC_REQ_MODE = 0x1e,
  GDC_SET_MODE = 0x1f,
  GDC_STOP = 0x21,
  GDC_GET_SCD = 0x22,
  GDC_REQ_SES = 0x23,
  GDC_REQ_STAT = 0x24,
  GDC_GET_VER = 0x28,
};

enum {
  GDC_STATUS_ERROR = -1,
  GDC_STATUS_INACTIVE = 0x0,
  GDC_STATUS_ACTIVE = 0x1,
  GDC_STATUS_COMPLETE = 0x2,
  GDC_STATUS_ABORT = 0x3,
};

enum {
  GDC_ERROR_OK = 0x0,
  GDC_ERROR_SYSTEM = 0x1,
  GDC_ERROR_NO_DISC = 0x2,
  GDC_ERROR_INVALID_CMD = 0x5,
  GDC_ERROR_DISC_CHANGE = 0x6,
};

static int bios_gdrom_override_format(struct bios *bios, int format) {
  struct dreamcast *dc = bios->dc;

  /* the IP.BIN bootstraps of some cdi discs patch the GDROM_CHECK_DRIVE syscall
     code to always return a disc format of GDROM instead of CDROM. i'm not sure
     of the exact reason behind this, but it seems that some games explicitly
     check that this syscall returns a format of GDROM on startup, so these
     patches are required to make the games boot

     however, since this patched syscall code isn't being ran, the patches need
     to be detected and their indended effect mimicked. complicating the matter,
     the patch routines won't apply the patch if they can't find a magic value
     from the real bios code near the patch site. since no bios is loaded, these
     values aren't found and the code isn't actually patched in the first place,
     making it hard to detect the patch by looking for writes to the code

     so far, the best idea i've had to work around this is to check the IP.BIN
     metadata to see if it calls itself a CD-ROM or GD-ROM. if it says GD-ROM,
     it's always treated as such */
  struct disc *disc = gdrom_get_disc(dc->gdrom);
  CHECK_NOTNULL(disc);

  if (strstr(disc->discnum, "GD-ROM") != NULL) {
    return GD_DISC_GDROM;
  }

  return format;
}

static uint32_t bios_gdrom_send_cmd(struct bios *bios, uint32_t cmd_code,
                                    uint32_t params) {
  struct dreamcast *dc = bios->dc;

  if (bios->status != GDC_STATUS_INACTIVE) {
    return 0;
  }

  uint32_t next_id = bios->cmd_id + 1;
  if (next_id == 0) {
    next_id = 1;
  }

  bios->status = GDC_STATUS_ACTIVE;
  bios->cmd_id = next_id;
  bios->cmd_code = cmd_code;

  memset(bios->params, 0, sizeof(bios->params));
  memset(bios->result, 0, sizeof(bios->result));

  if (params) {
    /* greedily copy 4 params every time and hope this doesn't blow up */
    sh4_memcpy_to_host(dc->mem, &bios->params, params, sizeof(bios->params));
  }

  return bios->cmd_id;
}

static void bios_gdrom_mainloop(struct bios *bios) {
  struct dreamcast *dc = bios->dc;
  struct gdrom *gd = dc->gdrom;
  struct holly *hl = dc->holly;

  if (bios->status != GDC_STATUS_ACTIVE) {
    return;
  }

  /* by default, all commands report that they've completed successfully */
  bios->status = GDC_STATUS_COMPLETE;

  switch (bios->cmd_code) {
    case GDC_PIOREAD:
    case GDC_DMAREAD: {
      int fad = bios->params[0];
      int num_sectors = bios->params[1];
      uint32_t dst = bios->params[2];
      uint32_t unknown = bios->params[3];
      int fmt = GD_SECTOR_ANY;
      int mask = GD_MASK_DATA;

      LOG_SYSCALL("GDC_DMAREAD fad=0x%x n=0x%x dst=0x%x unknown=0x%x", fad,
                  num_sectors, dst, unknown);

      /* dma read functionality changes somehow when this in non-zero */
      CHECK_EQ(unknown, 0);

      int read = 0;
      int rem = 0;
      uint8_t tmp[DISC_MAX_SECTOR_SIZE];

      for (int i = fad; i < fad + num_sectors; i++) {
        int n = gdrom_read_sectors(gd, i, 1, fmt, mask, tmp, sizeof(tmp));
        sh4_memcpy_to_guest(dc->mem, dst + read, tmp, n);
        read += n;
        rem -= n;
      }

      /* record size transferred */
      bios->result[2] = read;
      bios->result[3] = rem;
    } break;

    case GDC_GETTOC: {
      LOG_FATAL("GDC_GETTOC");
    } break;

    case GDC_GETTOC2: {
      uint32_t area = bios->params[0];
      uint32_t dst = bios->params[1];

      LOG_SYSCALL("GDC_GETTOC2 area=0x%x dst=0x%x", area, dst);

      struct gd_status_info stat;
      gdrom_get_status(gd, &stat);

      if (area == GD_AREA_HIGH && stat.format != GD_DISC_GDROM) {
        /* only GD-ROMs have a high-density area. in this situation, the bios
           doesn't set a result or error */
        bios->status = GDC_STATUS_INACTIVE;
      } else {
        struct gd_toc_info toc;
        gdrom_get_toc(gd, area, &toc);

        /* bit   |  7  |  6  |  5  |  4  |  3  |  2  |  1  |  0
           byte  |     |     |     |     |     |     |     |
           ------------------------------------------------------
           n*4+0 | track n fad (lsb)
           ------------------------------------------------------
           n*4+1 | track n fad
           ------------------------------------------------------
           n*4*2 | track n fad (msb)
           ------------------------------------------------------
           n*4+3 | track n control       | track n adr
           ------------------------------------------------------
           396   |  0  |  0  |  0  |  0  |  0  |  0  |  0  |  0
           ------------------------------------------------------
           397   |  0  |  0  |  0  |  0  |  0  |  0  |  0  |  0
           ------------------------------------------------------
           398   | start track number
           ------------------------------------------------------
           399   | start track control   | start track adr
           ------------------------------------------------------
           400   |  0  |  0  |  0  |  0  |  0  |  0  |  0  |  0
           ------------------------------------------------------
           401   |  0  |  0  |  0  |  0  |  0  |  0  |  0  |  0
           ------------------------------------------------------
           402   | end track number
           ------------------------------------------------------
           403   | end track control     | end track adr
           ------------------------------------------------------
           404   | lead-out track fad (lsb)
           ------------------------------------------------------
           405   | lead-out track fad
           ------------------------------------------------------
           406   | lead-out track fad (msb)
           ------------------------------------------------------
           407   | lead-out track ctrl   | lead-out track adr */
        uint8_t out[408];
        for (int i = 0; i < ARRAY_SIZE(toc.entries); i++) {
          struct gd_toc_entry *entry = &toc.entries[i];
          out[i * 4 + 0] = (entry->fad & 0x000000ff);
          out[i * 4 + 1] = (entry->fad & 0x0000ff00) >> 8;
          out[i * 4 + 2] = (entry->fad & 0x00ff0000) >> 16;
          out[i * 4 + 3] = ((entry->ctrl & 0xf) << 4) | (entry->adr & 0xf);
        }
        out[396] = 0;
        out[397] = 0;
        out[398] = toc.first.fad & 0xff;
        out[399] = ((toc.first.ctrl & 0xf) << 4) | (toc.first.adr & 0xf);
        out[400] = 0;
        out[401] = 0;
        out[402] = toc.last.fad & 0xff;
        out[403] = ((toc.last.ctrl & 0xf) << 4) | (toc.last.adr & 0xf);
        out[404] = (toc.leadout.fad & 0x000000ff);
        out[405] = (toc.leadout.fad & 0x0000ff00) >> 8;
        out[406] = (toc.leadout.fad & 0x00ff0000) >> 16;
        out[407] = ((toc.leadout.ctrl & 0xf) << 4) | (toc.leadout.adr & 0xf);
        sh4_memcpy_to_guest(dc->mem, dst, &out, sizeof(out));

        /* the bios doesn't perform a pio transfer to get the toc for this req,
           it is cached, so there is no transfer size to record */
      }
    } break;

    case GDC_PLAY: {
      LOG_WARNING("unsupported GDC_PLAY");
    } break;

    case GDC_PLAY2: {
      LOG_WARNING("unsupported GDC_PLAY2");
    } break;

    case GDC_PAUSE: {
      LOG_WARNING("unsupported GDC_PAUSE");
      /* TODO same as SPI_CD_SEEK with parameter type = pause playback */
    } break;

    case GDC_RELEASE: {
      LOG_WARNING("unsupported GDC_RELEASE");
    } break;

    case GDC_INIT: {
      LOG_SYSCALL("GDC_INIT");

      /* sanity check in case dma transfers are made async in the future */
      CHECK_EQ(*hl->SB_GDST, 0);
    } break;

    case GDC_SEEK: {
      LOG_WARNING("unsupported GDC_SEEK");
    } break;

    case GDC_READ: {
      LOG_WARNING("unsupported GDC_READ");
    } break;

    case GDC_REQ_MODE: {
      uint32_t dst = bios->params[0];

      LOG_SYSCALL("GDC_REQ_MODE 0x%x", dst);

      struct gd_hw_info info;
      gdrom_get_mode(gd, &info);

      /* bit   |  7  |  6  |  5  |  4  |  3  |  2  |  1  |  0
         byte  |     |     |     |     |     |     |     |
         ------------------------------------------------------
         0-3   | CD-ROM speed (little-endian)
         ------------------------------------------------------
         4-7   | standby time (little-endian)
         ------------------------------------------------------
         8-11  | read flags (little-endian)
         ------------------------------------------------------
         12-15 | read retry times (little-endian) */
      uint32_t out[4];
      out[0] = info.speed;
      out[1] = (info.standby_hi << 8) | info.standby_lo;
      out[2] = info.read_flags;
      out[3] = info.read_retry;
      sh4_memcpy_to_guest(dc->mem, dst, out, sizeof(out));

      /* record size of pio transfer to gdrom */
      bios->result[2] = 0xa;
    } break;

    case GDC_SET_MODE: {
      uint32_t speed = bios->params[0];
      uint32_t standby = bios->params[1];
      uint32_t read_flags = bios->params[2];
      uint32_t read_retry = bios->params[3];

      LOG_SYSCALL("GDC_SET_MODE 0x%x 0x%x 0x%x 0x%x", speed, standby,
                  read_flags, read_retry);

      struct gd_hw_info info;
      gdrom_get_mode(gd, &info);

      info.speed = speed;
      info.standby_hi = (standby & 0xff00) >> 8;
      info.standby_lo = standby & 0xff;
      info.read_flags = read_flags;
      info.read_retry = read_retry;
      gdrom_set_mode(gd, &info);

      /* record size of pio transfer to gdrom */
      bios->result[2] = 0xa;
    } break;

    case GDC_STOP: {
      LOG_FATAL("GDC_STOP");
      /* TODO same as SPI_CD_SEEK with parameter type = stop playback */
    } break;

    case GDC_GET_SCD: {
      uint32_t format = bios->params[0];
      uint32_t size = bios->params[1];
      uint32_t dst = bios->params[2];

      LOG_SYSCALL("GDC_GET_SCD fmt=0x%x size=0x%x dst=0x%x", format, size, dst);

      uint8_t scd[GD_SPI_SCD_SIZE];
      gdrom_get_subcode(gd, format, scd, sizeof(scd));
      CHECK_EQ(scd[3], size);

      /* TODO this is totally broken, fix once gdrom_get_subcode is actually
         implemented */
      sh4_memcpy_to_guest(dc->mem, dst, scd, size);

      /* record size of pio transfer to gdrom */
      bios->result[2] = size;
    } break;

    case GDC_REQ_SES: {
      LOG_FATAL("GDC_REQ_SES");
    } break;

    case GDC_REQ_STAT: {
      /* odd, but this function seems to get passed 4 unique pointers */
      uint32_t dst0 = bios->params[0];
      uint32_t dst1 = bios->params[1];
      uint32_t dst2 = bios->params[2];
      uint32_t dst3 = bios->params[3];

      LOG_SYSCALL(
          "GDC_REQ_STAT dst0=0x%08x dst1=0x%08x dst2=0x%08x dst3=0x%08x",
          bios->params[0], bios->params[1], bios->params[2], bios->params[3]);

      struct gd_status_info stat;
      gdrom_get_status(gd, &stat);

      /* bit   |  7  |  6  |  5  |  4  |  3  |  2  |  1  |  0
         byte  |     |     |     |     |     |     |     |
         ------------------------------------------------------
         0     |  0  |  0  |  0  |  0  | status
         ------------------------------------------------------
         1     |  0  |  0  |  0  |  0  | repeat count
         ------------------------------------------------------
         2-3   |  0  |  0  |  0  |  0  |  0  |  0  |  0  |  0 */
      uint32_t out = ((stat.repeat & 0xf) << 8) | (stat.status & 0xf);
      sh4_write32(dc->mem, dst0, out);

      /* bit   |  7  |  6  |  5  |  4  |  3  |  2  |  1  |  0
         byte  |     |     |     |     |     |     |     |
         ------------------------------------------------------
         0     | subcode q track number
         ------------------------------------------------------
         1-3   |  0  |  0  |  0  |  0  |  0  |  0  |  0  |  0 */
      out = (stat.scd_track & 0xff);
      sh4_write32(dc->mem, dst1, out);

      /* bit   |  7  |  6  |  5  |  4  |  3  |  2  |  1  |  0
         byte  |     |     |     |     |     |     |     |
         ------------------------------------------------------
         0-2  | fad (little-endian)
         ------------------------------------------------------
         3    | address          | control  */
      out = ((stat.address & 0xf) << 28) | ((stat.control & 0xf) << 24) |
            (stat.fad & 0x00ffffff);
      sh4_write32(dc->mem, dst2, out);

      /* bit   |  7  |  6  |  5  |  4  |  3  |  2  |  1  |  0
         byte  |     |     |     |     |     |     |     |
         ------------------------------------------------------
         0     | subcode q index number
         ------------------------------------------------------
         1-3   |  0  |  0  |  0  |  0  |  0  |  0  |  0  |  0 */
      out = (stat.scd_index & 0xff);
      sh4_write32(dc->mem, dst3, out);

      /* record pio transfer size */
      bios->result[2] = 0xa;
    } break;

    case GDC_GET_VER: {
      uint32_t dst = bios->params[0];

      LOG_SYSCALL("GDC_GET_VER dst=0x%x", dst);

      /* copy raw version string */
      char ver[] = "GDC Version 1.10 1999-03-31 ";
      int len = (int)strlen(ver);
      CHECK_EQ(len, 28);

      /* 0x8c0013b8 (offset 0xd0 in the gdrom state struct) is then loaded and
         overwrites the last byte. no idea what this is, but seems to be hard
         coded to 0x02 on boot */
      ver[len - 1] = 0x02;

      sh4_memcpy_to_guest(dc->mem, dst, ver, len);
    } break;

    default: {
      LOG_FATAL("bios_gdrom_mainloop unexpected cmd=0x%x", bios->cmd_code);
    } break;
  }
}

void bios_gdrom_vector(struct bios *bios) {
  struct dreamcast *dc = bios->dc;
  struct gdrom *gd = dc->gdrom;
  struct holly *hl = dc->holly;
  struct sh4_context *ctx = &dc->sh4->ctx;

  uint32_t misc = ctx->r[6];
  uint32_t fn = ctx->r[7];

  if (misc) {
    switch (fn) {
      case MISC_INIT: {
        /*
         * MISC_INIT
         *
         * initializes all the syscall vectors to their default values
         */
        LOG_FATAL("MISC_INIT");
      } break;

      case MISC_SETVECTOR: {
        /*
         * MISC_SETVECTOR
         *
         * sets/clears the handler for one of the eight superfunctions for this
         * vector. setting a handler is only allowed if it not currently set
         *
         * r4: superfunction number (0-7)
         * r5: pointer to handler function, or NULL to clear
         *
         * r0: zero if successful, -1 if setting/clearing the handler fails
         */
        LOG_FATAL("MISC_SETVECTOR");
      } break;

      default:
        LOG_FATAL("unexpected MISC syscall %d", fn);
        break;
    }
  } else {
    switch (fn) {
      case GDROM_SEND_COMMAND: {
        /*
         * GDROM_SEND_COMMAND
         *
         * enqueue a command for the gdrom to execute
         *
         * r4: command code
         * r5: pointer to parameter block for the command, can be NULL if the
         *     command does not take parameters
         *
         * r0: a request id (>0) if successful, negative error code if failed
         */
        uint32_t cmd_code = ctx->r[4];
        uint32_t params = ctx->r[5];
        uint32_t cmd_id = bios_gdrom_send_cmd(bios, cmd_code, params);

        LOG_SYSCALL("GDROM_SEND_COMMAND cmd_code=0x%x params=0x%x cmd_id=0x%x",
                    cmd_code, params, cmd_id);

        ctx->r[0] = cmd_id;
      } break;

      case GDROM_CHECK_COMMAND: {
        /*
         * GDROM_CHECK_COMMAND
         *
         * check if an enqueued command has completed
         *
         * r4: request id
         * r5: pointer to four 32 bit integers to receive extended status
         *     information. the first is a generic error code
         *
         * r0: 0, no such request active
         *     1, request is still being processed
         *     2, request has completed (if queried again, you will get a 0)
         *     3, request was aborted(?)
         *    -1, request has failed (examine extended status information for
         *        cause of failure)
         */
        uint32_t cmd_id = ctx->r[4];
        uint32_t status = ctx->r[5];

        LOG_SYSCALL("GDROM_CHECK_COMMAND 0x%x 0x%x", cmd_id, status);

        if (cmd_id != bios->cmd_id) {
          /* error if something other than the most recent command is checked */
          const uint32_t result[] = {GDC_ERROR_INVALID_CMD, 0, 0, 0};
          sh4_memcpy_to_guest(dc->mem, status, result, sizeof(result));
          ctx->r[0] = GDC_STATUS_ERROR;
        } else {
          sh4_memcpy_to_guest(dc->mem, status, bios->result,
                              sizeof(bios->result));
          ctx->r[0] = bios->status;

          /* clear result so nothing is returned if queried a second time */
          bios->status = GDC_STATUS_INACTIVE;
          memset(bios->result, 0, sizeof(bios->result));
        }
      } break;

      case GDROM_MAINLOOP: {
        /*
         * GDROM_MAINLOOP
         *
         * in order for enqueued commands to get processed, this function must
         * be called a few times. it can be called from a periodic interrupt, or
         * just keep calling it manually until GDROM_CHECK_COMMAND says that
         * your command has stopped processing
         */
        LOG_SYSCALL("GDROM_MAINLOOP");

        bios_gdrom_mainloop(bios);
      } break;

      case GDROM_INIT: {
        /*
         * GDROM_INIT
         *
         * initialize the gdrom subsystem. should be called before any requests
         * are enqueued
         */
        LOG_SYSCALL("GDROM_INIT");

        bios->status = GDC_STATUS_INACTIVE;
      } break;

      case GDROM_CHECK_DRIVE: {
        /*
         * GDROM_CHECK_DRIVE
         *
         * checks the general condition of the drive
         *
         * r4: pointer to two 32 bit integers, to receive the drive status. the
         *     first is the current drive status, the second is the type of disc
         *     inserted (if any)
         *
         *     drive status:  0x00, drive is busy
         *                    0x01, drive is paused
         *                    0x02, drive is in standby
         *                    0x03, drive is playing
         *                    0x04, drive is seeking
         *                    0x05, drive is scanning
         *                    0x06, drive lid is open
         *                    0x07, lid is closed, but there is no disc
         *
         *     disk format:   0x00, CDDA
         *                    0x10, CDROM
         *                    0x20, CDROM/XA
         *                    0x30, CDI
         *                    0x80, GDROM
         *
         * r0: zero if successful, nonzero if failure
         */
        uint32_t dst = ctx->r[4];

        LOG_SYSCALL("GDROM_CHECK_DRIVE dst=0x%x", dst);

        if (gdrom_is_busy(gd)) {
          /* shouldn't happen unless syscalls are interlaced with raw accesses
           */
          LOG_SYSCALL("GDROM_CHECK_DRIVE drive is busy");

          /* error */
          ctx->r[0] = 1;
        } else {
          struct gd_status_info stat;
          gdrom_get_status(gd, &stat);

          uint32_t cond[2];
          cond[0] = stat.status;
          cond[1] = bios_gdrom_override_format(bios, stat.format) << 4;
          sh4_memcpy_to_guest(dc->mem, dst, cond, sizeof(cond));

          /* success */
          ctx->r[0] = 0;
        }
      } break;

      case GDROM_G1_DMA_END: {
        /*
         * GDROM_G1_DMA_END
         *
         * r4: callback
         * r5: callback param
         */
        uint32_t callback = ctx->r[4];
        uint32_t param = ctx->r[5];

        LOG_SYSCALL("GDROM_G1_DMA_END 0x%x 0x%x", callback, param);

        holly_clear_interrupt(hl, HOLLY_INT_G1DEINT);

        /* TODO support callbacks */
        CHECK_EQ(callback, 0);
      } break;

      case GDROM_REQ_DMA: {
        /*
         * GDROM_REQ_DMA
         */
        LOG_FATAL("GDROM_REQ_DMA");
      } break;

      case GDROM_CHECK_DMA: {
        /*
         * GDROM_CHECK_DMA
         */
        /* read SB_GDST to check if DMA is in progress, if so, write out
           SB_GDLEND to r5 and return 1

           if no dma is in progress, write out amount of data available in
           DMA buffer and return 0 */
        LOG_FATAL("GDROM_CHECK_DMA");
      } break;

      case GDROM_ABORT_COMMAND: {
        /*
         * GDROM_ABORT_COMMAND
         *
         * tries to abort a previously enqueued command
         *
         * r4: request id
         *
         * r0: zero if successful, nonzero if failure
         */
        LOG_SYSCALL("GDROM_ABORT_COMMAND");

        /* all commands are performed immediately, there's nothing to cancel */
        ctx->r[0] = -1;
      } break;

      case GDROM_RESET: {
        /*
         * GDROM_RESET
         *
         * resets the drive
         */
        LOG_FATAL("GDROM_RESET");
      } break;

      case GDROM_SECTOR_MODE: {
        /*
         * GDROM_SECTOR_MODE
         *
         * sets/gets the sector format for read commands
         *
         * r4: pointer to a struct of four 32 bit integers containing new
         *     values or to receive the old values
         *
         *     field  function
         *     0      if 0 the mode will be set, if 1 it will be queried
         *     1      ? (always 8192)
         *     2      1024 = mode 1, 2048 = mode 2, 0 = auto detect
         *     3      sector size in bytes (normally 2048)
         *
         * r0: zero if successful, -1 if failure
         */
        LOG_FATAL("GDROM_SECTOR_MODE");
      } break;

      default:
        LOG_FATAL("unexpected GDROM syscall %d", fn);
        break;
    }
  }

  /* branch to the return address */
  ctx->pc = ctx->pr;
}

/*
 * flashrom syscalls
 */
enum {
  FLASHROM_INFO = 0,
  FLASHROM_READ = 1,
  FLASHROM_PROGRAM = 2,
  FLASHROM_ERASE = 3,
};

void bios_flashrom_vector(struct bios *bios) {
  struct dreamcast *dc = bios->dc;
  struct flash *flash = dc->flash;
  struct sh4_context *ctx = &dc->sh4->ctx;

  int fn = ctx->r[7];

  switch (fn) {
    case FLASHROM_INFO: {
      /*
       * FLASHROM_INFO
       *
       * queries the extent of a single partition in the system flashrom
       *
       * r4: partition number (0-4)
       * r5: pointer to two 32 bit integers to receive the result. the first
       *     will be the offset of the partition start, in bytes from the start
       *     of the flashrom. the second will be the size of the partition in
       *     bytes
       *
       * r0: zero if successful, -1 if no such partition exists
       */
      int part_id = ctx->r[4];
      uint32_t dst = ctx->r[5];

      LOG_SYSCALL("FLASHROM_INFO 0x%x 0x%x", part_id, dst);

      int offset, size;
      flash_partition_info(part_id, &offset, &size);

      uint32_t result[2];
      result[0] = offset;
      result[1] = size;
      sh4_memcpy_to_guest(dc->mem, dst, result, sizeof(result));

      ctx->r[0] = 0;
    } break;

    case FLASHROM_READ: {
      /*
       * FLASHROM_READ
       *
       * read data from the system flashrom
       *
       * r4: read start position, in bytes from the start of the flashrom
       * r5: pointer to destination buffer
       * r6: number of bytes to read
       *
       * r0: number of read bytes if successful, -1 if read failed
       */
      int offset = ctx->r[4];
      uint32_t dst = ctx->r[5];
      int size = ctx->r[6];

      LOG_SYSCALL("FLASHROM_READ 0x%x 0x%x 0x%x", offset, dst, size);

      uint8_t tmp[32];
      int read = 0;

      while (read < size) {
        int n = MIN(size - read, (int)sizeof(tmp));
        flash_read(flash, offset + read, tmp, n);
        sh4_memcpy_to_guest(dc->mem, dst + read, tmp, n);
        read += n;
      }

      ctx->r[0] = read;
    } break;

    case FLASHROM_PROGRAM: {
      /*
       * FLASHROM_PROGRAM
       *
       * write data to the system flashrom. important: it is only possible to
       * overwrite 1's with 0's, 0's can not be written back to 1's. general
       * overwriting is therefore not possible. only bytes containing all ones
       * can be written with arbitrary values
       *
       * r4: write start position, in bytes from the start of the flashrom
       * r5: pointer to source buffer
       * r6: number of bytes to write
       *
       * r0: number of written bytes if successful, -1 if write failed
       */
      int offset = ctx->r[4];
      uint32_t src = ctx->r[5];
      int size = ctx->r[6];

      LOG_SYSCALL("FLASHROM_PROGRAM 0x%x 0x%x 0x%x", offset, src, size);

      uint8_t tmp[32];
      int wrote = 0;

      while (wrote < size) {
        int n = MIN(size - wrote, (int)sizeof(tmp));
        sh4_memcpy_to_host(dc->mem, tmp, src + wrote, n);
        flash_program(flash, offset + wrote, tmp, n);
        wrote += n;
      }

      ctx->r[0] = wrote;
    } break;

    case FLASHROM_ERASE: {
      /*
       * FLASHROM_ERASE
       *
       * return a flashrom partition to all ones, so that it may be rewritten
       *
       * r4: offset of the start of the partition you want to delete, in bytes
       *     from the start of the flashrom
       *
       * r0: zero if successful, -1 if delete failed
       */
      uint32_t start = ctx->r[4];

      LOG_SYSCALL("FLASHROM_ERASE 0x%x", start);

      int part_id = 0;
      while (part_id < FLASH_PT_NUM) {
        int offset, size;
        flash_partition_info(part_id, &offset, &size);

        if (offset == (int)start) {
          break;
        }
      }
      CHECK_NE(part_id, FLASH_PT_NUM);

      flash_erase_partition(flash, part_id);

      ctx->r[0] = 0;
    } break;

    default:
      LOG_FATAL("unexpected FLASHROM syscall %d", fn);
      break;
  }

  /* branch to the return address */
  ctx->pc = ctx->pr;
}

/*
 * fontrom syscalls
 */
void bios_fontrom_vector(struct bios *bios) {
  struct dreamcast *dc = bios->dc;
  struct sh4_context *ctx = &dc->sh4->ctx;

  int fn = ctx->r[1];

  switch (fn) {
    case 0: {
      LOG_SYSCALL("FONTROM_ADDRESS");

      /* TODO embed a valid font and return the address to it here */
      ctx->r[0] = 0;
    } break;

    case 1: {
      LOG_SYSCALL("FONTROM_LOCK");

      /* success, mutex aquired */
      ctx->r[0] = 0;
    } break;

    case 2: {
      LOG_SYSCALL("FONTROM_UNLOCK");
    } break;

    default:
      LOG_FATAL("unknown FONTROM syscall %d", fn);
      break;
  }

  /* branch to the return address */
  ctx->pc = ctx->pr;
}

/*
 * sysinfo syscalls
 */
enum {
  SYSINFO_INIT = 0,
  SYSINFO_ICON = 2,
  SYSINFO_ID = 3,
};

void bios_sysinfo_vector(struct bios *bios) {
  struct dreamcast *dc = bios->dc;
  struct flash *flash = dc->flash;
  struct sh4_context *ctx = &dc->sh4->ctx;
  const uint32_t SYSINFO_DST = 0x8c000068;

  int fn = ctx->r[7];

  switch (fn) {
    case SYSINFO_INIT: {
      /*
       * SYSINFO_INIT
       *
       * prepares the other two SYSINFO calls for use by copying the relevant
       * data from the system flashrom into 0x8c000068-0x8c00007f. always call
       * this function before using the other two calls
       *
       * r0: zero if successful
       */
      LOG_SYSCALL("SYSINFO_INIT");

      /*
       * 0x00-0x07: system_id
       * 0x08-0x0c: system_props
       * 0x0d-0x0f: padding (zeroed out)
       * 0x10-0x17: settings (zeroed out)
        */
      uint8_t data[24] = {0};

      /* read system_id from 0x0001a056 */
      flash_read(flash, 0x1a056, &data[0], 8);

      /* read system_props from 0x0001a000 */
      flash_read(flash, 0x1a000, &data[8], 5);

      sh4_memcpy_to_guest(dc->mem, SYSINFO_DST, data, sizeof(data));

      ctx->r[0] = 0;
    } break;

    case SYSINFO_ICON: {
      /*
       * SYSINFO_ICON
       *
       * read an icon from the flashrom. the format those icons are in is not
       * known. SYSINFO_INIT must have been called first
       *
       * r4: icon number (0-9, but only 5-9 seems to really be icons)
       * r5: destination buffer (704 bytes in size)
       *
       * r0: number of read bytes if successful, negative if read failed
       */
      uint32_t icon = ctx->r[4];
      uint32_t dst = ctx->r[5];

      LOG_SYSCALL("SYSINFO_ICON  0x%x 0x%x", icon, dst);

      ctx->r[0] = 704;
    } break;

    case SYSINFO_ID: {
      /*
       * SYSINFO_ID
       *
       * query the unique 64 bit id number of this Dreamcast. SYSINFO_INIT must
       * have been called first
       *
       * r0: a pointer to where the id is stored as 8 contiguous bytes
       */
      LOG_SYSCALL("SYSINFO_ID");

      ctx->r[0] = SYSINFO_DST;
    } break;

    default:
      LOG_FATAL("unexpected SYSINFO syscall %d", fn);
      break;
  }

  /* branch to the return address */
  ctx->pc = ctx->pr;
}
