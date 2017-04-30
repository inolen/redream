#ifndef MAPLE_TYPES_H
#define MAPLE_TYPES_H

#define MAPLE_NUM_PORTS 4
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
  MAPLE_FUNC_MEMORYCARD = 0x02000000,
  MAPLE_FUNC_LCDDISPLAY = 0x04000000,
  MAPLE_FUNC_CLOCK = 0x08000000,
  MAPLE_FUNC_MICROPHONE = 0x10000000,
  MAPLE_FUNC_ARGUN = 0x20000000,
  MAPLE_FUNC_KEYBOARD = 0x40000000,
  MAPLE_FUNC_LIGHTGUN = 0x80000000,
  MAPLE_FUNC_PURUPURUPACK = 0x00010000,
  MAPLE_FUNC_MOUSE = 0x00020000
};

/* maple command codes. positive codes are commands and success responses,
   negative codes are error responses */
enum maple_cmd {
  MAPLE_REQ_DEVINFO = 1,
  MAPLE_REQ_DEVINFOEX = 2,
  MAPLE_REQ_DEVRESET = 3,
  MAPLE_REQ_DEVKILL = 4,
  MAPLE_RES_DEVINFO = 5,
  MAPLE_RES_DEVINFOEX = 6,
  MAPLE_RES_ACK = 7,
  MAPLE_RES_TRANSFER = 8,
  MAPLE_REQ_GETCOND = 9,
  MAPLE_REQ_GETMEMINFO = 10,
  MAPLE_REQ_BLOCKREAD = 11,
  MAPLE_REQ_BLOCKWRITE = 12,
  MAPLE_REQ_BLOCKSYNC = 13,
  MAPLE_REQ_SETCOND = 14,
  MAPLE_RES_NONE = -1,
  MAPLE_RES_BADFUNC = -2,
  MAPLE_RES_BADCMD = -3,
  MAPLE_RES_AGAIN = -4,
  MAPLE_RES_FILEERR = -5,
};

/* maple dma transfer descriptor */
union maple_transfer {
  struct {
    uint32_t length : 8;
    uint32_t pattern : 3;
    uint32_t reserved : 5;
    uint32_t port : 2;
    uint32_t reserved1 : 13;
    uint32_t last : 1;
  };
  uint32_t full;
};

/* first word in each frame sent on the maple bus */
union maple_header {
  struct {
    uint32_t command : 8;
    uint32_t recv_addr : 8;
    uint32_t send_addr : 8;
    uint32_t num_words : 8;
  };
  uint32_t full;
};

/* messages sent on the maple bus are sent as a "frame", with each frame
   consisting of one or more 32-bit words. the first word in each frame
   is the header */
struct maple_frame {
  union maple_header header;
  uint32_t params[0xff];
};

/* response to MAPLE_REQ_DEVINFO */
struct maple_device_info {
  uint32_t function;
  uint32_t function_data[3];
  uint8_t area_code;
  uint8_t connector_direction;
  char product_name[30];
  char product_license[60];
  uint16_t standby_power;
  uint16_t max_power;
};

/* response MAPLE_REQ_GETCOND */
struct maple_cond {
  uint32_t function;
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
  uint32_t function;
  uint16_t num_blocks;
  uint16_t partiion;
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

/* response to MAPLE_REQ_BLOCKREAD */
struct maple_blockread {
  uint32_t function;
  uint32_t block;
  uint32_t data[];
};

/* controller buttons */
enum {
  CONT_C,
  CONT_B,
  CONT_A,
  CONT_START,
  CONT_DPAD_UP,
  CONT_DPAD_DOWN,
  CONT_DPAD_LEFT,
  CONT_DPAD_RIGHT,
  CONT_Z,
  CONT_Y,
  CONT_X,
  CONT_D,
  CONT_DPAD2_UP,
  CONT_DPAD2_DOWN,
  CONT_DPAD2_LEFT,
  CONT_DPAD2_RIGHT,
  /* only used by internal button map */
  CONT_JOYX,
  CONT_JOYY,
  CONT_LTRIG,
  CONT_RTRIG,
  NUM_CONTROLS,
};

#endif
