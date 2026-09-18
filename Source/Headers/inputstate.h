#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef uint8_t KeyState;

enum {
  KEYSTATE_ACTIVE_BIT = 0b001,
  KEYSTATE_CHANGE_BIT = 0b010,
  KEYSTATE_IGNORE_BIT = 0b100,

  KEYSTATE_OFF = 0b000,
  KEYSTATE_PRESSED = KEYSTATE_ACTIVE_BIT | KEYSTATE_CHANGE_BIT,
  KEYSTATE_HELD = KEYSTATE_ACTIVE_BIT,
  KEYSTATE_UP = KEYSTATE_OFF | KEYSTATE_CHANGE_BIT,
  KEYSTATE_IGNOREHELD = KEYSTATE_OFF | KEYSTATE_IGNORE_BIT,
};

void UpdateKeyState(KeyState* state, bool downNow);
float ResolveAnalogInput(KeyState keyboardState, bool allowKeyboard,
                        bool gamepadOpen, float gamepadValue);
