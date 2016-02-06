#include <fstream>
#include <sstream>
#include <gflags/gflags.h>
#include <json11.hpp>
#include "core/log.h"
#include "hw/maple/maple_controller.h"

using namespace re::hw::maple;
using namespace re::sys;
using namespace json11;

DEFINE_string(profile, "", "Controller profile");

// Controller profile contains button mappings and other misc. configurable
// settings for the controller.
static Json default_profile =
    Json::object{{"joyx", ""},       {"joyy", ""},       {"ltrig", ""},
                 {"rtrig", ""},      {"start", "space"}, {"a", "k"},
                 {"b", "l"},         {"x", "j"},         {"y", "i"},
                 {"dpad_up", "w"},   {"dpad_down", "s"}, {"dpad_left", "a"},
                 {"dpad_right", "d"}};

MapleControllerProfile::MapleControllerProfile() : button_map_() {}

void MapleControllerProfile::Load(const char *path) {
  Json profile = default_profile;

  // load up the specified controller profile if set
  if (*path) {
    LOG_INFO("Loading controller profile %s", path);

    std::ifstream file(path);

    if (!file.is_open()) {
      LOG_WARNING("Failed to open %s", path);
    } else {
      std::stringstream stream;
      stream << file.rdbuf();

      std::string err;
      Json override = Json::parse(stream.str(), err);

      if (override.is_null()) {
        LOG_WARNING(err.c_str());
      } else {
        profile = override;
      }
    }
  }

  // map keys -> dreamcast buttons
  MapKey(profile["joyx"].string_value().c_str(), CONT_JOYX);
  MapKey(profile["joyy"].string_value().c_str(), CONT_JOYY);
  MapKey(profile["ltrig"].string_value().c_str(), CONT_LTRIG);
  MapKey(profile["rtrig"].string_value().c_str(), CONT_RTRIG);
  MapKey(profile["start"].string_value().c_str(), CONT_START);
  MapKey(profile["a"].string_value().c_str(), CONT_A);
  MapKey(profile["b"].string_value().c_str(), CONT_B);
  MapKey(profile["x"].string_value().c_str(), CONT_X);
  MapKey(profile["y"].string_value().c_str(), CONT_Y);
  MapKey(profile["dpad_up"].string_value().c_str(), CONT_DPAD_UP);
  MapKey(profile["dpad_down"].string_value().c_str(), CONT_DPAD_DOWN);
  MapKey(profile["dpad_left"].string_value().c_str(), CONT_DPAD_LEFT);
  MapKey(profile["dpad_right"].string_value().c_str(), CONT_DPAD_RIGHT);
}

void MapleControllerProfile::MapKey(const char *name, int button) {
  Keycode code = GetKeycodeByName(name);
  button_map_[code] = button;
}

// Constant device info structure sent as response to CMD_REQDEVINFO to
// identify the controller.
static MapleDevinfo controller_devinfo = {
    FN_CONTROLLER,
    {0xfe060f00, 0x0, 0x0},
    0xff,
    0,
    "Dreamcast Controller",
    "Produced By or Under License From SEGA ENTERPRISES,LTD.",
    0x01ae,
    0x01f4};

MapleController::MapleController() {
  state_.function = FN_CONTROLLER;
  // buttons bitfield contains 0s for pressed buttons and 1s for unpressed
  state_.buttons = 0xffff;
  // triggers completely unpressed
  state_.rtrig = state_.ltrig = 0;
  // joysticks default to dead center
  state_.joyy = state_.joyx = state_.joyx2 = state_.joyy2 = 0x80;

  // load profile
  profile_.Load(FLAGS_profile.c_str());
}

bool MapleController::HandleInput(Keycode key, int16_t value) {
  // map incoming key to dreamcast button
  int button = profile_.LookupButton(key);

  // scale incoming int16_t -> uint8_t
  uint8_t scaled = ((int32_t)value - INT16_MIN) >> 8;

  if (!button) {
    LOG_WARNING("Ignored key %s, no mapping found", GetNameByKeycode(key));
    return false;
  }

  if (button <= CONT_DPAD2_RIGHT) {
    if (value) {
      state_.buttons &= ~button;
    } else {
      state_.buttons |= button;
    }
  } else if (button == CONT_JOYX) {
    state_.joyx = scaled;
  } else if (button == CONT_JOYY) {
    state_.joyy = scaled;
  } else if (button == CONT_LTRIG) {
    state_.ltrig = scaled;
  } else if (button == CONT_RTRIG) {
    state_.rtrig = scaled;
  }

  return true;
}

bool MapleController::HandleFrame(const MapleFrame &frame, MapleFrame &res) {
  switch (frame.header.command) {
    case CMD_REQDEVINFO:
      res.header.command = CMD_RESDEVINFO;
      res.header.recv_addr = frame.header.send_addr;
      res.header.send_addr = frame.header.recv_addr;
      res.header.num_words = sizeof(controller_devinfo) >> 2;
      memcpy(&res.params, &controller_devinfo, sizeof(controller_devinfo));
      return true;

    case CMD_GETCONDITION:
      res.header.command = CMD_RESTRANSFER;
      res.header.recv_addr = frame.header.send_addr;
      res.header.send_addr = frame.header.recv_addr;
      res.header.num_words = sizeof(state_) >> 2;
      memcpy(&res.params, &state_, sizeof(state_));
      return true;
  }

  return false;
}
