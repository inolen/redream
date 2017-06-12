#ifndef FLASH_H
#define FLASH_H

#include "bios/flash_types.h"

struct flash;

void flash_partition_info(int part_id, int *offset, int *size, int *blocks);
void flash_erase_partition(struct flash *flash, int part_id);
int flash_read_block(struct flash *flash, int part_id, int block_id,
                     void *data);
int flash_write_block(struct flash *flash, int part_id, int block_id,
                      void *data);

#endif
