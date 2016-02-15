#ifndef MAPLE_H
#define MAPLE_H

#include <memory>
#include "sys/keycode.h"

namespace re {
namespace hw {
namespace holly {
class Holly;
}

struct Dreamcast;
struct Register;
class Memory;

namespace maple {

enum { MAX_PORTS = 4 };

enum MapleFunction {
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

enum MapleCommand {
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
  MDC_UNKNOWNFUNC = -2,
  CMD_UNKNOWNCMD = -3,
  CMD_RESENDCMD = -4,
  CMD_FILEERROR = -5
};

union MapleFrameHeader {
  struct {
    uint32_t command : 8;
    uint32_t recv_addr : 8;
    uint32_t send_addr : 8;
    uint32_t num_words : 8;
  };
  uint32_t full;
};

union MapleTransferDesc {
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

struct MapleDevinfo {
  uint32_t function;
  uint32_t function_data[3];
  uint8_t area_code;
  uint8_t connector_direction;
  char product_name[30];
  char product_license[60];
  uint16_t standby_power;
  uint16_t max_power;
};

struct MapleFrame {
  MapleFrameHeader header;
  uint32_t params[0xff];
};

class MapleDevice {
 public:
  virtual ~MapleDevice() {}

  virtual bool HandleInput(sys::Keycode key, int16_t value) = 0;
  virtual bool HandleFrame(const MapleFrame &frame, MapleFrame &res) = 0;
};

class Maple {
  friend class holly::Holly;

 public:
  Maple(Dreamcast *dc);

  bool Init();

  bool HandleInput(int port, sys::Keycode key, int16_t value);
  void VBlank();

 private:
  template <typename T>
  T ReadRegister(uint32_t addr);
  template <typename T>
  void WriteRegister(uint32_t addr, T value);

  bool HandleFrame(const MapleFrame &frame, MapleFrame &res);
  void StartDMA();

  Dreamcast *dc_;
  Memory *memory_;
  holly::Holly *holly_;
  Register *holly_regs_;

  std::unique_ptr<MapleDevice> devices_[MAX_PORTS];
};
}
}
}

#endif
