#ifndef FLASH_H
#define FLASH_H

#include "guest/bios/flash_types.h"

struct flash;

void flash_partition_info(int part_id, int *offset, int *size);

void flash_erase_partition(struct flash *flash, int part_id);

int flash_check_header(struct flash *flash, int part_id);
void flash_write_header(struct flash *flash, int part_id);

int flash_read_block(struct flash *flash, int part_id, int block_id,
                     void *data);
int flash_write_block(struct flash *flash, int part_id, int block_id,
                      const void *data);

#endif
