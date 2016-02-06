#ifndef MAPLE_CONTROLLER_H
#define MAPLE_CONTROLLER_H

#include "hw/maple/maple.h"

namespace re {
namespace hw {
namespace maple {

enum {
  CONT_C = 0x1,
  CONT_B = 0x2,
  CONT_A = 0x4,
  CONT_START = 0x8,
  CONT_DPAD_UP = 0x10,
  CONT_DPAD_DOWN = 0x20,
  CONT_DPAD_LEFT = 0x40,
  CONT_DPAD_RIGHT = 0x80,
  CONT_Z = 0x100,
  CONT_Y = 0x200,
  CONT_X = 0x400,
  CONT_D = 0x800,
  CONT_DPAD2_UP = 0x1000,
  CONT_DPAD2_DOWN = 0x2000,
  CONT_DPAD2_LEFT = 0x4000,
  CONT_DPAD2_RIGHT = 0x8000,
  // only used by internal button map
  CONT_JOYX = 0x10000,
  CONT_JOYY = 0x20000,
  CONT_LTRIG = 0x40000,
  CONT_RTRIG = 0x80000
};

struct MapleControllerState {
  uint32_t function;
  uint16_t buttons;
  uint8_t rtrig;
  uint8_t ltrig;
  uint8_t joyx;
  uint8_t joyy;
  uint8_t joyx2;
  uint8_t joyy2;
};

class MapleControllerProfile {
 public:
  MapleControllerProfile();

  void Load(const char *path);

  int LookupButton(sys::Keycode code) { return button_map_[code]; }

 private:
  void MapKey(const char *name, int button);

  int button_map_[sys::K_NUM_KEYS];
};

class MapleController : public MapleDevice {
 public:
  MapleController();

  bool HandleInput(sys::Keycode key, int16_t value);
  bool HandleFrame(const MapleFrame &frame, MapleFrame &res);

 private:
  MapleControllerState state_;
  MapleControllerProfile profile_;
};
}
}
}

#endif
