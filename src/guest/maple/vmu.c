#include "core/assert.h"
#include "core/filesystem.h"
#include "core/string.h"
#include "guest/maple/maple.h"
#include "guest/maple/vmu_default.inc"

#define VMU_BLOCK_SIZE 512
#define VMU_BLOCK_WORDS (512 >> 2)
#define VMU_BLOCK_OFFSET(blk, phase) \
  ((blk)*VMU_BLOCK_SIZE + (phase) * (VMU_BLOCK_SIZE >> 2))

struct vmu {
  struct maple_device;

  /* note, a persistent file handle isn't kept open here, writes are instead
     performed immediately to avoid corrupt saves in the event of a crash */
  char filename[PATH_MAX];
};

static void vmu_write_bin(struct vmu *vmu, int block, int phase,
                          const void *buffer, int num_words) {
  int offset = VMU_BLOCK_OFFSET(block, phase);
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
  int offset = VMU_BLOCK_OFFSET(block, phase);
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

static int vmu_frame(struct maple_device *dev, const struct maple_frame *frame,
                     struct maple_frame *res) {
  struct vmu *vmu = (struct vmu *)dev;

  switch (frame->header.command) {
    case MAPLE_REQ_DEVINFO: {
      /* based on captured result of real Dreamcast VMU */
      struct maple_device_info info;
      info.func = MAPLE_FUNC_MEMORYCARD;
      info.data[0] = 0x00410f00;
      info.region = 0xff;
      strncpy_pad_spaces(info.name, "Visual Memory", sizeof(info.name));
      strncpy_pad_spaces(
          info.license,
          "Produced By or Under License From SEGA ENTERPRISES,LTD.",
          sizeof(info.license));
      info.standby_power = 0x007c;
      info.max_power = 0x0082;

      res->header.command = MAPLE_RES_DEVINFO;
      res->header.recv_addr = frame->header.send_addr;
      res->header.send_addr = frame->header.recv_addr;
      res->header.num_words = sizeof(info) >> 2;
      memcpy(res->params, &info, sizeof(info));
      return 1;
    }

    case MAPLE_REQ_GETMEMINFO: {
      static struct maple_meminfo vmu_meminfo = {MAPLE_FUNC_MEMORYCARD,
                                                 0xff,
                                                 0x0,
                                                 0xff,
                                                 0xfe,
                                                 0x1,
                                                 0xfd,
                                                 0xd,
                                                 0x0,
                                                 0xc8,
                                                 0x1f,
                                                 {0x0, 0x0}};

      uint32_t func = frame->params[0];
      CHECK_EQ(func, MAPLE_FUNC_MEMORYCARD);
      uint32_t partition = frame->params[1] & 0xff;
      CHECK_EQ(partition, 0);

      res->header.command = MAPLE_RES_TRANSFER;
      res->header.recv_addr = frame->header.send_addr;
      res->header.send_addr = frame->header.recv_addr;
      res->header.num_words = sizeof(vmu_meminfo) >> 2;
      memcpy(res->params, &vmu_meminfo, sizeof(vmu_meminfo));
      return 1;
    }

    case MAPLE_REQ_BLOCKREAD: {
      uint32_t func = frame->params[0];
      CHECK_EQ(func, MAPLE_FUNC_MEMORYCARD);

      int partition, block, phase;
      vmu_parse_block_param(frame->params[1], &partition, &block, &phase);
      CHECK_EQ(partition, 0);
      CHECK_EQ(phase, 0);

      struct maple_blockread vmu_read = {MAPLE_FUNC_MEMORYCARD,
                                         frame->params[1]};

      res->header.command = MAPLE_RES_TRANSFER;
      res->header.recv_addr = frame->header.send_addr;
      res->header.send_addr = frame->header.recv_addr;
      res->header.num_words = (sizeof(vmu_read) >> 2) + VMU_BLOCK_WORDS;
      memcpy(res->params, &vmu_read, sizeof(vmu_read));
      vmu_read_bin(vmu, block, phase, &res->params[sizeof(vmu_read) >> 2],
                   VMU_BLOCK_WORDS);
      return 1;
    }

    case MAPLE_REQ_BLOCKWRITE: {
      uint32_t func = frame->params[0];
      CHECK_EQ(func, MAPLE_FUNC_MEMORYCARD);

      int partition, block, phase;
      vmu_parse_block_param(frame->params[1], &partition, &block, &phase);
      CHECK_EQ(partition, 0);

      vmu_write_bin(vmu, block, phase, &frame->params[2],
                    frame->header.num_words - 2);

      res->header.command = MAPLE_RES_ACK;
      res->header.recv_addr = frame->header.send_addr;
      res->header.send_addr = frame->header.recv_addr;
      res->header.num_words = 0;
      return 1;
    }

    case MAPLE_REQ_BLOCKSYNC: {
      res->header.command = MAPLE_RES_ACK;
      res->header.recv_addr = frame->header.send_addr;
      res->header.send_addr = frame->header.recv_addr;
      res->header.num_words = 0;
      return 1;
    }
  }

  return 0;
}

static void vmu_destroy(struct maple_device *dev) {
  struct vmu *vmu = (struct vmu *)dev;
  free(vmu);
}

struct maple_device *vmu_create(struct dreamcast *dc, int port, int unit) {
  struct vmu *vmu = calloc(1, sizeof(struct vmu));

  vmu->dc = dc;
  vmu->port = port;
  vmu->unit = unit;
  vmu->destroy = &vmu_destroy;
  vmu->frame = &vmu_frame;

  const char *appdir = fs_appdir();
  snprintf(vmu->filename, sizeof(vmu->filename),
           "%s" PATH_SEPARATOR "vmu_%d_%d.bin", appdir, vmu->port, vmu->unit);

  /* intialize default vmu if one doesn't already exist */
  if (!fs_exists(vmu->filename)) {
    LOG_INFO("initializing vmu at %s", vmu->filename);

    FILE *file = fopen(vmu->filename, "wb");
    CHECK_NOTNULL(file, "failed to open %s", vmu->filename);
    fwrite(vmu_default, 1, sizeof(vmu_default), file);
    fclose(file);
  }

  return (struct maple_device *)vmu;
}
