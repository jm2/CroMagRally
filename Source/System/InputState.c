#include "inputstate.h"

void UpdateKeyState(KeyState* state, bool downNow)
{
  switch (*state)
  {
  case KEYSTATE_HELD:
  case KEYSTATE_PRESSED:
    *state = downNow ? KEYSTATE_HELD : KEYSTATE_UP;
    break;

  case KEYSTATE_OFF:
  case KEYSTATE_UP:
  default:
    *state = downNow ? KEYSTATE_PRESSED : KEYSTATE_OFF;
    break;

  case KEYSTATE_IGNOREHELD:
    *state = downNow ? KEYSTATE_IGNOREHELD : KEYSTATE_OFF;
    break;
  }
}

float ResolveAnalogInput(KeyState keyboardState, bool allowKeyboard,
                        bool gamepadOpen, float gamepadValue)
{
  if (allowKeyboard && (keyboardState & KEYSTATE_ACTIVE_BIT))
    return 1.0f;

  return gamepadOpen ? gamepadValue : 0.0f;
}
