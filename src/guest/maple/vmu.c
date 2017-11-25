#include "core/core.h"
#include "core/filesystem.h"
#include "guest/maple/maple.h"
#include "guest/maple/vmu_default.inc"

#define BLK_SIZE 512
#define BLK_WORDS (512 >> 2)
#define BLK_OFFSET(blk, phase) ((blk)*BLK_SIZE + (phase) * (BLK_SIZE >> 2))

#define LCD_WIDTH 48
#define LCD_HEIGHT 32

struct vmu {
  struct maple_device;

  /* note, a persistent file handle isn't kept open here, writes are instead
     performed immediately to avoid corrupt saves in the event of a crash */
  char filename[PATH_MAX];
};

static void vmu_write_bin(struct vmu *vmu, int block, int phase,
                          const void *buffer, int num_words) {
  int offset = BLK_OFFSET(block, phase);
  int size = num_words << 2;

  FILE *file = fopen(vmu->filename, "r+b");
  CHECK_NOTNULL(file, "failed to open %s", vmu->filename);
  int r = fseek(file, offset, SEEK_SET);
  CHECK_NE(r, -1);
  r = (int)fwrite(buffer, 1, size, file);
  CHECK_EQ(r, size);
  fclose(file);
}

static void vmu_read_bin(struct vmu *vmu, int block, int phase, void *buffer,
                         int num_words) {
  int offset = BLK_OFFSET(block, phase);
  int size = num_words << 2;

  FILE *file = fopen(vmu->filename, "rb");
  CHECK_NOTNULL(file, "failed to open %s", vmu->filename);
  int r = fseek(file, offset, SEEK_SET);
  CHECK_NE(r, -1);
  r = (int)fread(buffer, 1, size, file);
  CHECK_EQ(r, size);
  fclose(file);
}

static void vmu_parse_block_param(uint32_t data, int *partition, int *block,
                                  int *phase) {
  /* 31-16               15-8   7-0
     block (big endian)  phase  partition */
  *partition = data & 0xff;
  *block = ((data >> 8) & 0xff00) | (data >> 24);
  *phase = (data >> 8) & 0xff;
}

static int vmu_frame(struct maple_device *dev, const union maple_frame *req,
                     union maple_frame *res) {
  struct vmu *vmu = (struct vmu *)dev;

  switch (req->cmd) {
    case MAPLE_REQ_DEVINFO: {
      char *name = "Visual Memory";
      char *license = "Produced By or Under License From SEGA ENTERPRISES,LTD.";
      struct maple_device_info info;
      info.func = MAPLE_FUNC_CLOCK | MAPLE_FUNC_LCD | MAPLE_FUNC_MEMCARD;
      info.data[0] = 0x403f7e7e; /* clock */
      info.data[1] = 0x00100500; /* lcd */
      info.data[2] = 0x00410f00; /* memcard */
      info.region = 0xff;
      strncpy_pad_spaces(info.name, name, sizeof(info.name));
      strncpy_pad_spaces(info.license, license, sizeof(info.license));
      info.standby_power = 0x007c;
      info.max_power = 0x0082;

      res->cmd = MAPLE_RES_DEVINFO;
      res->num_words = sizeof(info) >> 2;
      memcpy(res->params, &info, sizeof(info));
    } break;

    case MAPLE_REQ_GETMEMINFO: {
      uint32_t func = req->params[0];
      CHECK_EQ(func, MAPLE_FUNC_MEMCARD);
      uint32_t partition = req->params[1] & 0xff;
      CHECK_EQ(partition, 0);

      struct maple_meminfo meminfo = {0};
      meminfo.func = MAPLE_FUNC_MEMCARD;
      meminfo.num_blocks = 0xff;
      meminfo.partition = 0x0;
      meminfo.root_block = 0xff;
      meminfo.fat_block = 0xfe;
      meminfo.fat_num_blocks = 0x1;
      meminfo.dir_block = 0xfd;
      meminfo.dir_num_blocks = 0xd;
      meminfo.icon = 0x0;
      meminfo.data_block = 0xc8;
      meminfo.data_num_blocks = 0x1f;

      res->cmd = MAPLE_RES_TRANSFER;
      res->num_words = sizeof(meminfo) >> 2;
      memcpy(res->params, &meminfo, sizeof(meminfo));
    } break;

    case MAPLE_REQ_BLKREAD: {
      uint32_t func = req->params[0];
      CHECK_EQ(func, MAPLE_FUNC_MEMCARD);

      switch (func) {
        case MAPLE_FUNC_MEMCARD: {
          int partition, block, phase;
          vmu_parse_block_param(req->params[1], &partition, &block, &phase);
          CHECK_EQ(partition, 0);
          CHECK_EQ(phase, 0);

          struct maple_blkread blkread = {0};
          blkread.func = MAPLE_FUNC_MEMCARD;
          blkread.block = req->params[1];

          res->cmd = MAPLE_RES_TRANSFER;
          res->num_words = (sizeof(blkread) >> 2) + BLK_WORDS;
          memcpy(res->params, &blkread, sizeof(blkread));
          vmu_read_bin(vmu, block, phase, &res->params[sizeof(blkread) >> 2],
                       BLK_WORDS);
        } break;

        default:
          res->cmd = MAPLE_RES_BADFUNC;
          break;
      }
    } break;

    case MAPLE_REQ_BLKWRITE: {
      uint32_t func = req->params[0];

      switch (func) {
        case MAPLE_FUNC_MEMCARD: {
          int partition, block, phase;
          vmu_parse_block_param(req->params[1], &partition, &block, &phase);
          CHECK_EQ(partition, 0);

          vmu_write_bin(vmu, block, phase, &req->params[2], req->num_words - 2);

          res->cmd = MAPLE_RES_ACK;
        } break;

        case MAPLE_FUNC_LCD: {
#if 0
          /* TODO print this somewhere */
          const uint8_t *data = (const uint8_t *)&req->params[2];
          for (int y = LCD_HEIGHT-1; y >= 0; y--) {
            for (int x = LCD_WIDTH-1; x >= 0; x--) {
              int byte = x / 8;
              int mask = 0x80 >> (x % 8);
              if (data[y * (LCD_WIDTH / 8) + byte] & mask) {
                printf("*");
              } else {
                printf(" ");
              }
            }
            printf("\n");
          }
#endif
          res->cmd = MAPLE_RES_ACK;
        } break;

        default:
          res->cmd = MAPLE_RES_BADFUNC;
          break;
      }
    } break;

    case MAPLE_REQ_BLKSYNC:
      res->cmd = MAPLE_RES_ACK;
      break;

    case MAPLE_REQ_SETCOND: {
      uint32_t func = req->params[0];

      switch (func) {
        case MAPLE_FUNC_CLOCK: {
#if 0
          /* TODO emit a a beep sound */
          uint32_t beep = req->params[1];
#endif
          res->cmd = MAPLE_RES_ACK;
        } break;

        default:
          res->cmd = MAPLE_RES_BADFUNC;
          break;
      }
    } break;

    default:
      res->cmd = MAPLE_RES_BADCMD;
      break;
  }

  return 1;
}

static void vmu_destroy(struct maple_device *dev) {
  struct vmu *vmu = (struct vmu *)dev;
  free(vmu);
}

struct maple_device *vmu_create(struct maple *mp, int port) {
  struct vmu *vmu = calloc(1, sizeof(struct vmu));

  vmu->mp = mp;
  vmu->destroy = &vmu_destroy;
  vmu->frame = &vmu_frame;

  /* intialize default vmu if one doesn't exist */
  const char *appdir = fs_appdir();
  snprintf(vmu->filename, sizeof(vmu->filename),
           "%s" PATH_SEPARATOR "vmu%d.bin", appdir, port);

  if (!fs_exists(vmu->filename)) {
    LOG_INFO("vmu_create initializing %s", vmu->filename);

    FILE *file = fopen(vmu->filename, "wb");
    CHECK_NOTNULL(file, "failed to open %s", vmu->filename);
    fwrite(vmu_default, 1, sizeof(vmu_default), file);
    fclose(file);
  }

  return (struct maple_device *)vmu;
}
