#include "guest/bios/syscalls.h"
#include "guest/bios/bios.h"
#include "guest/bios/flash.h"
#include "guest/gdrom/gdrom.h"
#include "guest/holly/holly.h"
#include "guest/rom/flash.h"
#include "guest/sh4/sh4.h"

#if 0
#define LOG_SYSCALL LOG_INFO
#else
#define LOG_SYSCALL(...)
#endif

/*
 * menu syscalls
 */
void bios_menu_vector(struct bios *bios) {
  struct dreamcast *dc = bios->dc;
  struct sh4_context *ctx = &dc->sh4->ctx;

  uint32_t fn = ctx->r[4];

  LOG_SYSCALL("MENU 0x%x", fn);

  /* TODO returning 0 is totally broken, some games (e.g. Virtua Fighter 3b)
     seem to use these functions to soft reset the machine or something */

  /* nop, branch to the return address */
  ctx->pc = ctx->pr;
}

/*
 * gdrom syscalls
 */
enum {
  MISC_INIT = 0,
  MISC_SETVECTOR = 1,
};

enum {
  GDROM_SEND_COMMAND = 0,
  GDROM_CHECK_COMMAND = 1,
  GDROM_MAINLOOP = 2,
  GDROM_INIT = 3,
  GDROM_CHECK_DRIVE = 4,
  GDROM_G1_DMA_END = 5,
  GDROM_REQ_DMA = 6,
  GDROM_CHECK_DMA = 7,
  GDROM_ABORT_COMMAND = 8,
  GDROM_RESET = 9,
  GDROM_SECTOR_MODE = 10,
};

enum {
  GDC_PIOREAD = 16,
  GDC_DMAREAD = 17,
  GDC_GETTOC = 18,
  GDC_GETTOC2 = 19,
  GDC_PLAY = 20,
  GDC_PLAY2 = 21,
  GDC_PAUSE = 22,
  GDC_RELEASE = 23,
  GDC_INIT = 24,
  GDC_SEEK = 27,
  GDC_READ = 28,
  GDC_REQ_MODE = 30,
  GDC_SET_MODE = 31,
  GDC_STOP = 33,
  GDC_GET_SCD = 34,
  GDC_REQ_SES = 35,
  GDC_REQ_STAT = 36,
  GDC_GET_VER = 40,
};

enum {
  GDC_STATUS_NONE = 0,
  GDC_STATUS_ACTIVE = 1,
  GDC_STATUS_DONE = 2,
  GDC_STATUS_ABORT = 3,
  GDC_STATUS_ERROR = 4,
};

enum {
  GDC_ERROR_OK = 0,
  GDC_ERROR_NO_DISC = 2,
  GDC_ERROR_DISC_CHANGE = 6,
  GDC_ERROR_SYSTEM = 1,
};

static uint32_t bios_gdrom_send_cmd(struct bios *bios, uint32_t cmd_code,
                                    uint32_t params) {
  struct dreamcast *dc = bios->dc;
  struct gdrom *gd = dc->gdrom;
  struct holly *hl = dc->holly;
  struct sh4 *sh4 = dc->sh4;
  struct address_space *space = sh4->memory_if->space;

  if (bios->status != GDC_STATUS_NONE) {
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
    as_memcpy_to_host(space, &bios->params, params, sizeof(bios->params));
  }

  return bios->cmd_id;
}

static void bios_gdrom_mainloop(struct bios *bios) {
  struct dreamcast *dc = bios->dc;
  struct gdrom *gd = dc->gdrom;
  struct holly *hl = dc->holly;
  struct sh4_context *ctx = &dc->sh4->ctx;
  struct address_space *space = dc->sh4->memory_if->space;

  if (bios->status != GDC_STATUS_ACTIVE) {
    return;
  }

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
      uint8_t tmp[DISC_MAX_SECTOR_SIZE];

      for (int i = fad; i < fad + num_sectors; i++) {
        int n = gdrom_read_sectors(gd, i, 1, fmt, mask, tmp, sizeof(tmp));
        as_memcpy_to_guest(space, dst + read, tmp, n);
        read += n;
      }

      bios->result[2] = read;
      /* result[3] seems to signals if data is remaining, calculated by:
         r = (*0xa05f7018 & 0x88) == 0 ? 0 : 2
         since dmas are performed instantly, this should always be zero */
      bios->result[3] = 0;
    } break;

    case GDC_GETTOC: {
      LOG_FATAL("GDC_GETTOC");
    } break;

    case GDC_GETTOC2: {
      uint32_t area = bios->params[0];
      uint32_t dst = bios->params[1];

      LOG_SYSCALL("GDC_GETTOC2 0=0x%x 1=0x%x", area, dst);

      struct gd_spi_toc toc;
      gdrom_get_toc(gd, area, &toc);

      /* TODO check that this format is correct */
      as_memcpy_to_guest(space, dst, &toc, sizeof(toc));

      /* record size transferred */
      bios->result[2] = sizeof(toc);
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
      LOG_WARNING("GDC_RELEASE");
    } break;

    case GDC_INIT: {
      /* this seems to always immediately follow GDROM_INIT */
    } break;

    case GDC_SEEK: {
      LOG_FATAL("GDC_SEEK");
    } break;

    case GDC_READ: {
      LOG_FATAL("GDC_READ");
    } break;

    case GDC_REQ_MODE: {
      uint32_t dst = bios->params[0];

      LOG_SYSCALL("GDC_REQ_MODE 0x%x", dst);

      struct gd_hw_info info;
      gdrom_get_drive_mode(gd, &info);

      uint32_t mode[4];
      mode[0] = info.speed;
      mode[1] = (info.standby_hi << 8) | info.standby_lo;
      mode[2] = info.read_flags;
      mode[3] = info.read_retry;

      as_memcpy_to_guest(space, dst, mode, sizeof(mode));

      /* record size transferred */
      bios->result[2] = sizeof(mode);
    } break;

    case GDC_SET_MODE: {
      uint32_t speed = bios->params[0];
      uint32_t standby = bios->params[1];
      uint32_t read_flags = bios->params[2];
      uint32_t read_retry = bios->params[3];

      LOG_SYSCALL("GDC_SET_MODE 0x%x 0x%x 0x%x 0x%x", speed, standby,
                  read_flags, read_retry);

      struct gd_hw_info info;
      gdrom_get_drive_mode(gd, &info);

      info.speed = speed;
      info.standby_hi = (standby & 0xff00) >> 8;
      info.standby_lo = standby & 0xff;
      info.read_flags = read_flags;
      info.read_retry = read_retry;

      gdrom_set_drive_mode(gd, &info);
    } break;

    case GDC_STOP: {
      LOG_FATAL("GDC_STOP");
      /* TODO same as SPI_CD_SEEK with parameter type = stop playback */
    } break;

    case GDC_GET_SCD: {
      uint32_t format = bios->params[0];
      uint32_t size = bios->params[1];
      uint32_t dst = bios->params[2];

      LOG_SYSCALL("GDC_GET_SCD 0x%x 0x%x 0x%x", format, size, dst);

      uint8_t scd[GD_SPI_SCD_SIZE];
      gdrom_get_subcode(gd, format, scd, sizeof(scd));

      CHECK_EQ(scd[3], size);

      as_memcpy_to_guest(space, dst, scd, size);

      /* record size transferred */
      bios->result[2] = size;
    } break;

    case GDC_REQ_SES: {
      LOG_FATAL("GDC_REQ_SES");
    } break;

    case GDC_REQ_STAT: {
      LOG_SYSCALL("GDC_REQ_STAT");

      struct gd_spi_status stat;
      gdrom_get_status(gd, &stat);

      /* TODO verify this format */
      bios->result[0] = (stat.repeat << 8) | stat.status;
      bios->result[1] = stat.scd_track;
      bios->result[2] = stat.fad;
      bios->result[3] = stat.scd_index;
    } break;

    case GDC_GET_VER: {
      uint32_t dst = bios->params[0];

      LOG_SYSCALL("GDC_GET_VER 0x%x", dst);

      const char *version = "GDC Version 1.10 1999-03-31";
      const int len = strlen(version);

      as_memcpy_to_guest(space, dst, version, len);

      /* record size transferred */
      bios->result[2] = len;
    } break;

    default: { LOG_FATAL("unexpected gdrom cmd 0x%x", bios->cmd_code); } break;
  }

  bios->status = GDC_STATUS_DONE;
}

void bios_gdrom_vector(struct bios *bios) {
  struct dreamcast *dc = bios->dc;
  struct gdrom *gd = dc->gdrom;
  struct holly *hl = dc->holly;
  struct sh4_context *ctx = &dc->sh4->ctx;
  struct address_space *space = dc->sh4->memory_if->space;

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

        if (cmd_id == bios->cmd_id) {
          ctx->r[0] = bios->status;

          if (bios->status == GDC_STATUS_DONE) {
            as_memcpy_to_guest(space, status, &bios->result,
                               sizeof(bios->result));
            bios->status = GDC_STATUS_NONE;
          }
        } else {
          ctx->r[0] = GDC_STATUS_NONE;
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

        bios->status = GDC_STATUS_NONE;
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
        uint32_t result = ctx->r[4];

        LOG_SYSCALL("GDROM_CHECK_DRIVE 0x%x", result);

        struct gd_spi_status stat;
        gdrom_get_status(gd, &stat);

        uint32_t cond[2];
        cond[0] = stat.status;
        cond[1] = stat.format << 4;

        as_memcpy_to_guest(space, result, cond, sizeof(cond));

        /* success */
        ctx->r[0] = 0;
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
  struct address_space *space = dc->sh4->memory_if->space;

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
      as_memcpy_to_guest(space, dst, result, sizeof(result));

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
        as_memcpy_to_guest(space, dst + read, tmp, n);
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
        as_memcpy_to_host(space, tmp, src + wrote, n);
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
  struct address_space *space = dc->sh4->memory_if->space;
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
       * 0x8c000068-6f: system_id
       * 0x8c000070-74: system_props
       * 0x8c000075-77: padding
       * 0x8c000078-7f: settings
       */
      LOG_SYSCALL("SYSINFO_INIT");

      uint8_t data[24];

      /* read system_id from 0x0001a056 */
      flash_read(flash, 0x1a056, &data[0], 8);

      /* read system_props from 0x0001a000 */
      flash_read(flash, 0x1a000, &data[8], 5);

      /* system settings seem to always be zeroed out */
      memset(&data[13], 0, 11);

      as_memcpy_to_guest(space, SYSINFO_DST, data, sizeof(data));
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
