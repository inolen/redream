#include "keycode.h"
#include "core/core.h"

struct key {
  int code;
  const char *name;
};

static struct key keys[] = {
    {K_UNKNOWN, "unknown"},
    {K_SPACE, "space"},
    {'!', "!"},
    {'"', "\""},
    {'#', "#"},
    {'$', "$"},
    {'%', "%%"},
    {'&', "&"},
    {'\'', "\'"},
    {'(', "("},
    {')', ")"},
    {'*', "*"},
    {'+', "+"},
    {',', ","},
    {'-', "-"},
    {'.', "."},
    {'/', "/"},
    {'0', "0"},
    {'1', "1"},
    {'2', "2"},
    {'3', "3"},
    {'4', "4"},
    {'5', "5"},
    {'6', "6"},
    {'7', "7"},
    {'8', "8"},
    {'9', "9"},
    {':', ":"},
    {';', ";"},
    {'<', "<"},
    {'=', "="},
    {'>', ">"},
    {'?', "?"},
    {'@', "@"},
    {'[', "["},
    {'\\', "\\"},
    {']', "]"},
    {'^', "^"},
    {'_', "_"},
    {'`', "`"},
    {'a', "a"},
    {'b', "b"},
    {'c', "c"},
    {'d', "d"},
    {'e', "e"},
    {'f', "f"},
    {'g', "g"},
    {'h', "h"},
    {'i', "i"},
    {'j', "j"},
    {'k', "k"},
    {'l', "l"},
    {'m', "m"},
    {'n', "n"},
    {'o', "o"},
    {'p', "p"},
    {'q', "q"},
    {'r', "r"},
    {'s', "s"},
    {'t', "t"},
    {'u', "u"},
    {'v', "v"},
    {'w', "w"},
    {'x', "x"},
    {'y', "y"},
    {'z', "z"},
    {'{', "{"},
    {'|', "|"},
    {'}', "}"},
    {'~', "~"},
    {K_CAPSLOCK, "capslock"},
    {K_RETURN, "return"},
    {K_ESCAPE, "escape"},
    {K_BACKSPACE, "backspace"},
    {K_TAB, "tab"},
    {K_PAGEUP, "pageup"},
    {K_PAGEDOWN, "pagedown"},
    {K_DELETE, "delete"},
    {K_RIGHT, "right"},
    {K_LEFT, "left"},
    {K_DOWN, "down"},
    {K_UP, "up"},
    {K_LCTRL, "lctrl"},
    {K_LSHIFT, "lshift"},
    {K_LALT, "lalt"},
    {K_LGUI, "lgui"},
    {K_RCTRL, "rctrl"},
    {K_RSHIFT, "rshift"},
    {K_RALT, "ralt"},
    {K_RGUI, "rgui"},
    {K_F1, "f1"},
    {K_F2, "f2"},
    {K_F3, "f3"},
    {K_F4, "f4"},
    {K_F5, "f5"},
    {K_F6, "f6"},
    {K_F7, "f7"},
    {K_F8, "f8"},
    {K_F9, "f9"},
    {K_F10, "f10"},
    {K_F11, "f11"},
    {K_F12, "f12"},
    {K_F13, "f13"},
    {K_F14, "f14"},
    {K_F15, "f15"},
    {K_F16, "f16"},
    {K_F17, "f17"},
    {K_F18, "f18"},
    {K_F19, "f19"},
    {K_F20, "f20"},
    {K_F21, "f21"},
    {K_F22, "f22"},
    {K_F23, "f23"},
    {K_F24, "f24"},
    {K_MOUSE1, "mouse1"},
    {K_MOUSE2, "mouse2"},
    {K_MOUSE3, "mouse3"},
    {K_MOUSE4, "mouse4"},
    {K_MOUSE5, "mouse5"},
    {K_MWHEELUP, "mwheelup"},
    {K_MWHEELDOWN, "mwheeldown"},
};

int get_key_by_name(const char *keyname) {
  char buffer[256] = {0};
  int len = 0;

  while (*keyname) {
    buffer[len++] = tolower(*keyname);
    keyname++;
  }

  for (size_t i = 0, l = sizeof(keys) / sizeof(struct key); i < l; i++) {
    const struct key *key = &keys[i];

    if (!strcmp(key->name, buffer)) {
      return key->code;
    }
  }

  return K_UNKNOWN;
}

const char *get_name_by_key(int keycode) {
  static const char *unknown = "unknown";

  for (size_t i = 0, l = sizeof(keys) / sizeof(struct key); i < l; i++) {
    const struct key *key = &keys[i];

    if (key->code == keycode) {
      return key->name;
    }
  }

  return unknown;
}
