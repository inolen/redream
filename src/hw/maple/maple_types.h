#ifndef MAPLE_TYPES_H
#define MAPLE_TYPES_H

enum maple_fn {
  FN_CONTROLLER = 0x01000000,
  FN_MEMORYCARD = 0x02000000,
  FN_LCDDISPLAY = 0x04000000,
  FN_CLOCK = 0x08000000,
  FN_MICROPHONE = 0x10000000,
  FN_ARGUN = 0x20000000,
  FN_KEYBOARD = 0x40000000,
  FN_LIGHTGUN = 0x80000000,
  FN_PURUPURUPACK = 0x00010000,
  FN_MOUSE = 0x00020000
};

enum maple_cmd {
  CMD_REQDEVINFO = 1,
  CMD_REQDEVINFOEX = 2,
  CMD_RESETDEV = 3,
  CMD_SHUTDOWNDDEV = 4,
  CMD_RESDEVINFO = 5,
  CMD_RESDEVINFOEX = 6,
  CMD_RESACK = 7,
  CMD_RESTRANSFER = 8,
  CMD_GETCONDITION = 9,
  CMD_GETMEMORY = 10,
  CMD_BLOCKREAD = 11,
  CMD_BLOCKWRITE = 12,
  CMD_SETCONDITION = 14,
  CMD_NORESPONSE = -1,
  CMD_UNKNOWNFUNC = -2,
  CMD_UNKNOWNCMD = -3,
  CMD_RESENDCMD = -4,
  CMD_FILEERROR = -5
};

union maple_header {
  struct {
    uint32_t command : 8;
    uint32_t recv_addr : 8;
    uint32_t send_addr : 8;
    uint32_t num_words : 8;
  };
  uint32_t full;
};

struct maple_frame {
  union maple_header header;
  uint32_t params[0xff];
};

union maple_transfer {
  struct {
    uint32_t length : 8;
    uint32_t pattern : 3;
    uint32_t reserved : 5;
    uint32_t port : 2;
    uint32_t reserved1 : 13;
    uint32_t last : 1;
    uint32_t result_addr : 32;
  };
  uint64_t full;
};

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

#endif
