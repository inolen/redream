/*
 * high-level code for working with the system's flash memory
 */

#include "guest/bios/flash.h"
#include "core/core.h"
#include "guest/rom/flash.h"

#define FLASH_BLOCK_SIZE 0x40

/* each bitmap is 64 bytes in length, and each byte can record the state of 8
   physical blocks (one per bit), therefore, each bitmap can represent up to
   512 physical blocks. these 512 blocks are each 64-bytes in length, meaning
   each partition would need partition_size / 32768 bitmap blocks to represent
   all of its physical blocks  */
#define FLASH_BITMAP_BLOCKS (FLASH_BLOCK_SIZE * 8)
#define FLASH_BITMAP_BYTES (FLASH_BITMAP_BLOCKS * 64)

static int flash_crc_block(struct flash_user_block *block) {
  const uint8_t *buf = (const uint8_t *)block;
  int size = 62;
  int n = 0xffff;

  for (int i = 0; i < size; i++) {
    n ^= (buf[i] << 8);

    for (int c = 0; c < 8; c++) {
      if (n & 0x8000) {
        n = (n << 1) ^ 4129;
      } else {
        n = (n << 1);
      }
    }
  }

  return (~n) & 0xffff;
}

static int flash_validate_crc(struct flash_user_block *user) {
  return user->crc == flash_crc_block(user);
}

static inline int flash_num_physical_blocks(int size) {
  return size / FLASH_BLOCK_SIZE;
}

static inline int flash_num_bitmap_blocks(int size) {
  return (int)ceil(size / (float)FLASH_BITMAP_BYTES);
}

static inline int flash_num_user_blocks(int size) {
  return flash_num_physical_blocks(size) - flash_num_bitmap_blocks(size) - 1;
}

static inline int flash_is_allocated(uint8_t *bitmap, int phys_id) {
  int index = (phys_id - 1) % FLASH_BITMAP_BLOCKS;
  return (bitmap[index / 8] & (0x80 >> (index % 8))) == 0x0;
}

static inline void flash_set_allocated(uint8_t *bitmap, int phys_id) {
  int index = (phys_id - 1) % FLASH_BITMAP_BLOCKS;
  bitmap[index / 8] &= ~(0x80 >> (index % 8));
}

static void flash_write_physical_block(struct flash *flash, int offset,
                                       int phys_id, const void *data) {
  flash_write(flash, offset + phys_id * FLASH_BLOCK_SIZE, data,
              FLASH_BLOCK_SIZE);
}

static void flash_read_physical_block(struct flash *flash, int offset,
                                      int phys_id, void *data) {
  flash_read(flash, offset + phys_id * FLASH_BLOCK_SIZE, data,
             FLASH_BLOCK_SIZE);
}

static int flash_validate_header(struct flash *flash, int offset, int part_id) {
  struct flash_header_block header;
  flash_read_physical_block(flash, offset, 0, &header);

  if (memcmp(header.magic, FLASH_MAGIC_COOKIE, sizeof(header.magic)) != 0) {
    return 0;
  }

  if (header.part_id != part_id) {
    return 0;
  }

  return 1;
}

static int flash_alloc_block(struct flash *flash, int offset, int size) {
  uint8_t bitmap[FLASH_BLOCK_SIZE];
  int blocks = flash_num_user_blocks(size);
  int bitmap_id = blocks;
  int phys_id = 1;
  int phys_end = 1 + blocks;

  while (phys_id < phys_end) {
    /* read the next bitmap every FLASH_BITMAP_BLOCKS */
    if (phys_id % FLASH_BITMAP_BLOCKS == 1) {
      flash_read_physical_block(flash, offset, ++bitmap_id, bitmap);
    }

    /* use the first unallocated block */
    if (!flash_is_allocated(bitmap, phys_id)) {
      break;
    }

    phys_id++;
  }

  CHECK_LT(phys_id, phys_end, "partition has no more valid user blocks");

  /* mark the block as allocated */
  flash_set_allocated(bitmap, phys_id);
  flash_write_physical_block(flash, offset, bitmap_id, bitmap);

  return phys_id;
}

static int flash_lookup_block(struct flash *flash, int offset, int size,
                              int block_id) {
  uint8_t bitmap[FLASH_BLOCK_SIZE];
  int blocks = flash_num_user_blocks(size);
  int bitmap_id = 1 + blocks;
  int phys_id = 1;
  int phys_end = bitmap_id;

  /* in order to lookup a logical block, all physical blocks must be iterated.
     since physical blocks are allocated linearly, the physical block with the
     highest address takes precedence */
  int result = 0;

  while (phys_id < phys_end) {
    /* read the next bitmap every FLASH_BITMAP_BLOCKS */
    if (phys_id % FLASH_BITMAP_BLOCKS == 1) {
      flash_read_physical_block(flash, offset, bitmap_id++, bitmap);
    }

    /* being that phyiscal blocks are allocated linearlly, stop processing once
       the first unallocated block is hit */
    if (!flash_is_allocated(bitmap, phys_id)) {
      break;
    }

    struct flash_user_block user;
    flash_read_physical_block(flash, offset, phys_id, &user);

    if (user.block_id == block_id) {
      if (!flash_validate_crc(&user)) {
        LOG_WARNING("flash_lookup_block physical block %d has an invalid crc",
                    phys_id);
      } else {
        result = phys_id;
      }
    }

    phys_id++;
  }

  return result;
}

int flash_write_block(struct flash *flash, int part_id, int block_id,
                      const void *data) {
  int offset, size;
  flash_partition_info(part_id, &offset, &size);

  if (!flash_validate_header(flash, offset, part_id)) {
    return 0;
  }

  /* the real system libraries allocate and write to a new physical block each
     time a logical block is updated. the reason being that, flash memory can
     only be programmed once, and after that the entire sector must be reset in
     order to reprogram it. flash storage has a finite number of these erase
     operations before its integrity deteriorates, so the libraries try to
     minimize how often they occur by writing to a new physical block until the
     partition is completely full

     this limitation of the original hardware isn't a problem for us, so try and
     just update an existing logical block if it exists */
  int phys_id = flash_lookup_block(flash, offset, size, block_id);
  if (!phys_id) {
    phys_id = flash_alloc_block(flash, offset, size);

    if (!phys_id) {
      return 0;
    }
  }

  /* update the block's crc before writing it back out */
  struct flash_user_block user;
  memcpy(&user, data, sizeof(user));
  user.block_id = block_id;
  user.crc = flash_crc_block(&user);

  flash_write_physical_block(flash, offset, phys_id, &user);

  return 1;
}

int flash_read_block(struct flash *flash, int part_id, int block_id,
                     void *data) {
  int offset, size;
  flash_partition_info(part_id, &offset, &size);

  if (!flash_validate_header(flash, offset, part_id)) {
    return 0;
  }

  int phys_id = flash_lookup_block(flash, offset, size, block_id);
  if (!phys_id) {
    return 0;
  }

  flash_read_physical_block(flash, offset, phys_id, data);

  return 1;
}

void flash_write_header(struct flash *flash, int part_id) {
  int offset, size;
  flash_partition_info(part_id, &offset, &size);

  struct flash_header_block header;
  memset(&header, 0xff, sizeof(header));
  memcpy(header.magic, FLASH_MAGIC_COOKIE, sizeof(header.magic));
  header.part_id = part_id;
  header.version = 0;

  flash_write_physical_block(flash, offset, 0, &header);
}

int flash_check_header(struct flash *flash, int part_id) {
  int offset, size;
  flash_partition_info(part_id, &offset, &size);

  return flash_validate_header(flash, offset, part_id);
}

void flash_erase_partition(struct flash *flash, int part_id) {
  int offset, size;
  flash_partition_info(part_id, &offset, &size);

  flash_erase(flash, offset, size);
}

void flash_partition_info(int part_id, int *offset, int *size) {
  switch (part_id) {
    case FLASH_PT_FACTORY:
      *offset = 0x1a000;
      *size = 8 * 1024;
      break;
    case FLASH_PT_RESERVED:
      *offset = 0x18000;
      *size = 8 * 1024;
      break;
    case FLASH_PT_USER:
      *offset = 0x1c000;
      *size = 16 * 1024;
      break;
    case FLASH_PT_GAME:
      *offset = 0x10000;
      *size = 32 * 1024;
      break;
    case FLASH_PT_UNKNOWN:
      *offset = 0x00000;
      *size = 64 * 1024;
      break;
    default:
      LOG_FATAL("unknown partiton %d", part_id);
      break;
  }
}
