#ifndef MAPLE_TYPES_H
#define MAPLE_TYPES_H

#include <stdint.h>

/* number of ports on the maple bus */
#define MAPLE_NUM_PORTS 4

/* number of addressable units on each maple port */
#define MAPLE_MAX_UNITS 6

/* maple pattern codes. indicate how to process the incoming instruction */
enum maple_pattern {
  MAPLE_PATTERN_NORMAL = 0x0,
  MAPLE_PATTERN_GUN = 0x2,
  MAPLE_PATTERN_RESET = 0x3,
  MAPLE_PATTERN_GUN_RETURN = 0x4,
  MAPLE_PATTERN_NOP = 0x7,
};

/* maple function codes. these act to further specify the intent of some
   commands. for example, when a block write cmd is issued to a VMU it can
   either either write to the lcd screen, or the flash storage based on the
   function code */
enum maple_fn {
  MAPLE_FUNC_CONTROLLER = 0x01000000,
  MAPLE_FUNC_MEMCARD = 0x02000000,
  MAPLE_FUNC_LCD = 0x04000000,
  MAPLE_FUNC_CLOCK = 0x08000000,
  MAPLE_FUNC_MICROPHONE = 0x10000000,
  MAPLE_FUNC_ARGUN = 0x20000000,
  MAPLE_FUNC_KEYBOARD = 0x40000000,
  MAPLE_FUNC_LIGHTGUN = 0x80000000,
  MAPLE_FUNC_PURUPURU = 0x00010000,
  MAPLE_FUNC_MOUSE = 0x00020000
};

/* maple command codes. positive codes are commands and success responses,
   negative codes are error responses */
enum maple_cmd {
  MAPLE_REQ_DEVINFO = (uint8_t)1,
  MAPLE_REQ_DEVINFOEX = (uint8_t)2,
  MAPLE_REQ_DEVRESET = (uint8_t)3,
  MAPLE_REQ_DEVKILL = (uint8_t)4,
  MAPLE_RES_DEVINFO = (uint8_t)5,
  MAPLE_RES_DEVINFOEX = (uint8_t)6,
  MAPLE_RES_ACK = (uint8_t)7,
  MAPLE_RES_TRANSFER = (uint8_t)8,
  MAPLE_REQ_GETCOND = (uint8_t)9,
  MAPLE_REQ_GETMEMINFO = (uint8_t)10,
  MAPLE_REQ_BLKREAD = (uint8_t)11,
  MAPLE_REQ_BLKWRITE = (uint8_t)12,
  MAPLE_REQ_BLKSYNC = (uint8_t)13,
  MAPLE_REQ_SETCOND = (uint8_t)14,
  MAPLE_RES_NONE = (uint8_t)-1,
  MAPLE_RES_BADFUNC = (uint8_t)-2,
  MAPLE_RES_BADCMD = (uint8_t)-3,
  MAPLE_RES_AGAIN = (uint8_t)-4,
  MAPLE_RES_FILEERR = (uint8_t)-5,
};

/* maple dma transfer descriptor */
union maple_transfer {
  struct {
    uint32_t length : 8;
    uint32_t pattern : 3;
    uint32_t : 5;
    uint32_t port : 2;
    uint32_t : 13;
    uint32_t end : 1;
  };
  uint32_t full;
};

/* messages sent on the maple bus are sent as a "frame", with each frame
   consisting of 1-256 32-bit words. the first word in each frame is the
   header */
union maple_frame {
  struct {
    uint32_t cmd : 8;
    uint32_t dst_addr : 8;
    uint32_t src_addr : 8;
    uint32_t num_words : 8;
    uint32_t params[];
  };
  uint32_t data[0x100];
};

/* response to MAPLE_REQ_DEVINFO */
struct maple_device_info {
  /* function codes supported by this peripheral */
  uint32_t func;
  /* additional data for the function codes (3 max) */
  uint32_t data[3];
  /* region code of peripheral */
  uint8_t region;
  /* physical orientation of bus connection */
  uint8_t direction;
  /* name of peripheral */
  char name[30];
  /* license statement */
  char license[60];
  /* standby power consumption */
  uint16_t standby_power;
  /* max power consumption */
  uint16_t max_power;
};

/* response MAPLE_REQ_GETCOND */
struct maple_cond {
  uint32_t func;
  /* buttons bitfield contains 0s for pressed buttons and 1s for unpressed */
  uint16_t buttons;
  /* opposite of the buttons, 0 is unpressed for the triggers */
  uint8_t rtrig;
  uint8_t ltrig;
  /* dead center for the joysticks is 0x80 */
  uint8_t joyx;
  uint8_t joyy;
  uint8_t joyx2;
  uint8_t joyy2;
};

/* response to MAPLE_REQ_GETMEMINFO */
struct maple_meminfo {
  uint32_t func;
  uint16_t num_blocks;
  uint16_t partition;
  uint16_t root_block;
  uint16_t fat_block;
  uint16_t fat_num_blocks;
  uint16_t dir_block;
  uint16_t dir_num_blocks;
  uint16_t icon;
  uint16_t data_block;
  uint16_t data_num_blocks;
  uint16_t reserved[2];
};

/* response to MAPLE_REQ_BLKREAD */
struct maple_blkread {
  uint32_t func;
  uint32_t block;
  uint32_t data[];
};

#endif
