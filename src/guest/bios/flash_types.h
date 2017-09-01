#ifndef FLASH_TYPES_H
#define FLASH_TYPES_H

#include <stdint.h>

/* flash partitions */
#define FLASH_PT_FACTORY 0
#define FLASH_PT_RESERVED 1
#define FLASH_PT_USER 2
#define FLASH_PT_GAME 3
#define FLASH_PT_UNKNOWN 4
#define FLASH_PT_NUM 5

/* flash logical blocks */
#define FLASH_USER_SYSCFG 0x05

/* system region settings */
#define FLASH_REGION_JAPAN 0
#define FLASH_REGION_USA 1
#define FLASH_REGION_EUROPE 2

/* system language settings */
#define FLASH_LANG_JAPANESE 0
#define FLASH_LANG_ENGLISH 1
#define FLASH_LANG_GERMAN 2
#define FLASH_LANG_FRENCH 3
#define FLASH_LANG_SPANISH 4
#define FLASH_LANG_ITALIAN 5

/* system broadcast settings */
#define FLASH_BCAST_NTSC 0
#define FLASH_BCAST_PAL 1
#define FLASH_BCAST_PAL_M 2
#define FLASH_BCAST_PAL_N 3

/* magic cookie every block-allocated partition begins with */
#define FLASH_MAGIC_COOKIE "KATANA_FLASH____"

/* header block in block-allocated partition */
struct flash_header_block {
  char magic[16];
  uint8_t part_id;
  uint8_t version;
  uint8_t reserved[46];
};

/* user block in block-allocated partition */
struct flash_user_block {
  uint16_t block_id;
  uint8_t data[60];
  uint16_t crc;
};

struct flash_syscfg_block {
  uint16_t block_id;
  /* last set time (seconds since 1/1/1950 00:00) */
  uint16_t time_lo;
  uint16_t time_hi;
  uint8_t unknown1;
  uint8_t lang;
  uint8_t mono;
  uint8_t autostart;
  uint8_t unknown2[4];
  uint8_t reserved[50];
};

#endif
