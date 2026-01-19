// SDL INPUT
// (C) 2022 Iliyas Jorio
// This file is part of Cro-Mag Rally. https://github.com/jorio/CroMagRally

#include "game.h"

extern SDL_Window *gSDLWindow;

/***************/
/* CONSTANTS   */
/***************/

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

#define kJoystickDeadZoneFrac (.33f)
#define kJoystickDeadZoneFrac_UI (.66f)

/**********************/
/*     PROTOTYPES     */
/**********************/

typedef uint8_t KeyState;

typedef struct Gamepad {
  bool open;
  bool fallbackToKeyboard;
  SDL_Gamepad *sdlGamepad;
  KeyState needStates[NUM_CONTROL_NEEDS];
  float needAnalog[NUM_CONTROL_NEEDS];
} Gamepad;

Boolean gUserPrefersGamepad = false;

static Boolean gPlayerGamepadMappingLocked = false;
Gamepad gGamepads[MAX_LOCAL_PLAYERS];

static KeyState gKeyboardStates[SDL_SCANCODE_COUNT];
static KeyState gMouseButtonStates[NUM_SUPPORTED_MOUSE_BUTTONS];
static KeyState gNeedStates[NUM_CONTROL_NEEDS];

Boolean gMouseMotionNow = false;
char gTextInput[64];

static void OnJoystickRemoved(SDL_JoystickID which);
static SDL_Gamepad *TryOpenGamepadFromJoystick(SDL_JoystickID joystickID);
static SDL_Gamepad *TryOpenAnyUnusedGamepad(bool showMessage);
static int GetGamepadSlotFromJoystick(SDL_JoystickID joystickID);

#if defined(__ANDROID__) || defined(__IOS__)
static SDL_Sensor *gAccelerometer = NULL;

typedef struct {
  SDL_FingerID id;
  float x, y;
  bool active;
} VirtualFinger;

#define MAX_TOUCH_FINGERS 10
static VirtualFinger gFingers[MAX_TOUCH_FINGERS];
static SDL_JoystickID gVirtualJoystickID = 0;
static SDL_Joystick *gVirtualJoystick = NULL;
static bool gJoystickFingerActive = false;
static SDL_FingerID gJoystickFingerID = 0;

typedef struct {
  float stickX, stickY;
  bool btnA, btnB, btnX, btnY, btnStart;
} VirtualInputState;
static VirtualInputState gVirtualInput = {0};
#endif

#pragma mark -
/**********************/

#if defined(__ANDROID__) || defined(__IOS__)
static void InitMobileInput(void) {
  // Check for stale static state (Android process reuse)
  if (gVirtualJoystickID != 0 && gVirtualJoystick != NULL) {
    if (SDL_GetJoystickFromID(gVirtualJoystickID) == NULL) {
      SDL_Log("Detected stale Virtual Joystick ID! Resetting input state.");
      gVirtualJoystickID = 0;
      gVirtualJoystick = NULL;
      gJoystickFingerActive = false;
      gJoystickFingerID = 0;
      // Wipe fingers to be safe
      for (int i = 0; i < MAX_TOUCH_FINGERS; i++)
        gFingers[i].active = false;
    }
  }

  if (!gVirtualJoystickID) {
    SDL_Log("Initializing Virtual Gamepad...");

    SDL_VirtualJoystickDesc desc;
    SDL_INIT_INTERFACE(&desc);
    desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
    desc.naxes = 6;
    desc.nbuttons = 15;
    desc.nhats = 1;
    desc.vendor_id = 0x1234;
    desc.product_id = 0x5678;
    desc.name = "Cro-Mag Virtual Gamepad";

    gVirtualJoystickID = SDL_AttachVirtualJoystick(&desc);
    if (gVirtualJoystickID) {
      gVirtualJoystick = SDL_OpenJoystick(gVirtualJoystickID);
      SDL_Log("Virtual Gamepad added with ID %u", (uint32_t)gVirtualJoystickID);
    } else {
      SDL_Log("Failed to add Virtual Gamepad: %s", SDL_GetError());
    }
  }

  if (!gAccelerometer) {
    // Ensure touch events generate mouse events for menus
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "1");

    int num_sensors = 0;
    SDL_SensorID *sensorIDs = SDL_GetSensors(&num_sensors);
    if (sensorIDs) {
      for (int i = 0; i < num_sensors; ++i) {
        if (SDL_GetSensorTypeForID(sensorIDs[i]) == SDL_SENSOR_ACCEL) {
          gAccelerometer = SDL_OpenSensor(sensorIDs[i]);
          if (gAccelerometer)
            break;
        }
      }
      SDL_free(sensorIDs);
    }
  }

  // SDL_memset(gFingers, 0, sizeof(gFingers)); // DO NOT WIPE FINGERS! Persist
  // across frames.
}

static void UpdateVirtualGamepad(void) {
  if (!gVirtualJoystick)
    return;

  float stickX = 0, stickY = 0;
  bool btnA = false, btnB = false, btnX = false, btnY = false, btnStart = false;

  // ALWAYS process touch input to populate gVirtualInput for Input Injection.
  // This ensures touch works even when gUserPrefersGamepad is true (BT
  // controller present).
  for (int i = 0; i < MAX_TOUCH_FINGERS; i++) {
    if (!gFingers[i].active)
      continue;

    float x = gFingers[i].x;
    float y = gFingers[i].y;

    // Check for Sticky Joystick Ownership
    bool isJoystickFinger = false;
    if (gJoystickFingerActive && gJoystickFingerID == gFingers[i].id) {
      isJoystickFinger = true;
    } else if (!gJoystickFingerActive && x < 0.4f && y > 0.4f) {
      // New claim!
      gJoystickFingerActive = true;
      gJoystickFingerID = gFingers[i].id;
      isJoystickFinger = true;
      SDL_Log("STICK CLAIM: Finger=%lu at (%.2f, %.2f)",
              (unsigned long)gFingers[i].id, x, y);
    }

    if (isJoystickFinger) {
      float dx = (x - 0.27f) / 0.15f;
      float dy = (y - 0.85f) / 0.15f;
      if (dx < -1)
        dx = -1;
      if (dx > 1)
        dx = 1;
      if (dy < -1)
        dy = -1;
      if (dy > 1)
        dy = 1;
      stickX = dx;
      stickY = dy;
      SDL_Log("STICK: Finger=%lu X=%.2f Y=%.2f -> dX=%.2f dY=%.2f",
              (unsigned long)gFingers[i].id, x, y, stickX, stickY);
    }
    // Right side: Buttons (center: 0.85, 0.75, radius roughly 0.2)
    else if (x > 0.6f && y > 0.4f) {
      float dx = x - 0.85f;
      float dy = y - 0.75f;
      float distSq = dx * dx + dy * dy;
      if (distSq < 0.2f * 0.2f) {
        if (dy > 0.02f)
          btnA = true;
        else if (dx > 0.02f)
          btnB = true;
        else if (dx < -0.02f)
          btnX = true;
        else if (dy < -0.02f)
          btnY = true;
      }
    }
    // Top Right: Start (Pause) - expanded area for easier activation
    else if (x > 0.8f && y < 0.25f) {
      btnStart = true;
    }
  }

  // Update Global Virtual Input State (for Injection into Player 1)
  gVirtualInput.stickX = stickX;
  gVirtualInput.stickY = stickY;
  gVirtualInput.btnA = btnA;
  gVirtualInput.btnB = btnB;
  gVirtualInput.btnX = btnX;
  gVirtualInput.btnY = btnY;
  gVirtualInput.btnStart = btnStart;

  // Only send to SDL Virtual Joystick device when NOT using physical gamepad.
  // This prevents the virtual device from conflicting with the physical one.
  if (!gUserPrefersGamepad) {
    SDL_SetJoystickVirtualAxis(gVirtualJoystick, SDL_GAMEPAD_AXIS_LEFTX,
                               (int16_t)(stickX * 32767));
    SDL_SetJoystickVirtualAxis(gVirtualJoystick, SDL_GAMEPAD_AXIS_LEFTY,
                               (int16_t)(stickY * 32767));
    SDL_SetJoystickVirtualButton(gVirtualJoystick, SDL_GAMEPAD_BUTTON_SOUTH,
                                 btnA);
    SDL_SetJoystickVirtualButton(gVirtualJoystick, SDL_GAMEPAD_BUTTON_EAST,
                                 btnB);
    SDL_SetJoystickVirtualButton(gVirtualJoystick, SDL_GAMEPAD_BUTTON_WEST,
                                 btnX);
    SDL_SetJoystickVirtualButton(gVirtualJoystick, SDL_GAMEPAD_BUTTON_NORTH,
                                 btnY);
    SDL_SetJoystickVirtualButton(gVirtualJoystick, SDL_GAMEPAD_BUTTON_START,
                                 btnStart);
  }
}
#endif

static inline void UpdateKeyState(KeyState *state, bool downNow) {
  switch (*state) // look at prev state
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

void InvalidateNeedState(int need) { gNeedStates[need] = KEYSTATE_IGNOREHELD; }

void InvalidateAllInputs(void) {
  _Static_assert(1 == sizeof(KeyState),
                 "sizeof(KeyState) has changed -- Rewrite this function "
                 "without SDL_memset()!");

  SDL_memset(gNeedStates, KEYSTATE_IGNOREHELD, NUM_CONTROL_NEEDS);
  SDL_memset(gKeyboardStates, KEYSTATE_IGNOREHELD, SDL_SCANCODE_COUNT);
  SDL_memset(gMouseButtonStates, KEYSTATE_IGNOREHELD,
             NUM_SUPPORTED_MOUSE_BUTTONS);

  for (int i = 0; i < MAX_LOCAL_PLAYERS; i++) {
    SDL_memset(gGamepads[i].needStates, KEYSTATE_IGNOREHELD, NUM_CONTROL_NEEDS);
  }
}

static void UpdateRawKeyboardStates(void) {
  int numkeys = 0;
  const bool *keystate = SDL_GetKeyboardState(&numkeys);

  int minNumKeys = GAME_MIN(numkeys, SDL_SCANCODE_COUNT);

  for (int i = 0; i < minNumKeys; i++)
    UpdateKeyState(&gKeyboardStates[i], keystate[i]);

  // fill out the rest
  for (int i = minNumKeys; i < SDL_SCANCODE_COUNT; i++)
    UpdateKeyState(&gKeyboardStates[i], false);
}

static void ParseAltEnter(void) {
  if (GetNewKeyState(SDL_SCANCODE_RETURN) &&
      (GetKeyState(SDL_SCANCODE_LALT) || GetKeyState(SDL_SCANCODE_RALT))) {
    gGamePrefs.fullscreen = !gGamePrefs.fullscreen;
    SetFullscreenMode(false);

    InvalidateAllInputs();
  }
}

static void UpdateMouseButtonStates(int mouseWheelDelta) {
  uint32_t mouseButtons = SDL_GetMouseState(NULL, NULL);

  for (int i = 1; i < NUM_SUPPORTED_MOUSE_BUTTONS_PURESDL;
       i++) // SDL buttons start at 1!
  {
    bool buttonBit = 0 != (mouseButtons & SDL_BUTTON_MASK(i));
    UpdateKeyState(&gMouseButtonStates[i], buttonBit);
  }

  // Fake buttons for mouse wheel up/down
  UpdateKeyState(&gMouseButtonStates[SDL_BUTTON_WHEELUP], mouseWheelDelta > 0);
  UpdateKeyState(&gMouseButtonStates[SDL_BUTTON_WHEELDOWN],
                 mouseWheelDelta < 0);
}

static void UpdateInputNeeds(void) {
  for (int i = 0; i < NUM_CONTROL_NEEDS; i++) {
    const InputBinding *kb = &gGamePrefs.bindings[i];

    bool downNow = false;

    for (int j = 0; j < MAX_BINDINGS_PER_NEED; j++) {
      int16_t scancode = kb->key[j];
      if (scancode && scancode < SDL_SCANCODE_COUNT) {
        downNow |= gKeyboardStates[scancode] & KEYSTATE_ACTIVE_BIT;
      }
    }

    //		downNow |= gMouseButtonStates[kb->mouseButton] &
    //KEYSTATE_ACTIVE_BIT;

    UpdateKeyState(&gNeedStates[i], downNow);
  }
}

static void UpdateGamepadSpecificInputNeeds(int gamepadNum) {
  Gamepad *gamepad = &gGamepads[gamepadNum];

  if (!gamepad->open) {
    return;
  }

  SDL_Gamepad *sdlGamepad = gamepad->sdlGamepad;

  for (int needNum = 0; needNum < NUM_CONTROL_NEEDS; needNum++) {
    const InputBinding *kb = &gGamePrefs.bindings[needNum];

    float deadZoneFrac = needNum >= NUM_REMAPPABLE_NEEDS
                             ? kJoystickDeadZoneFrac_UI
                             : kJoystickDeadZoneFrac;

    float actuation = 0;

    for (int buttonNum = 0; buttonNum < MAX_BINDINGS_PER_NEED; buttonNum++) {
      const PadBinding *pb = &kb->pad[buttonNum];
      int type = pb->type;

      if (type == kInputTypeButton) {
        if (0 != SDL_GetGamepadButton(sdlGamepad, pb->id)) {
          actuation = 1;
        }

#if defined(__ANDROID__) || defined(__IOS__)
        // Inject Virtual Gamepad Button
        if (gamepadNum == 0 && !gUserPrefersGamepad) {
          if ((pb->id == SDL_GAMEPAD_BUTTON_SOUTH && gVirtualInput.btnA) ||
              (pb->id == SDL_GAMEPAD_BUTTON_EAST && gVirtualInput.btnB) ||
              (pb->id == SDL_GAMEPAD_BUTTON_WEST && gVirtualInput.btnX) ||
              (pb->id == SDL_GAMEPAD_BUTTON_NORTH && gVirtualInput.btnY) ||
              (pb->id == SDL_GAMEPAD_BUTTON_START && gVirtualInput.btnStart)) {
            actuation = 1;
          }
        }
#endif
      } else if (type == kInputTypeAxisPlus || type == kInputTypeAxisMinus) {
        float value;
        int16_t axis = SDL_GetGamepadAxis(sdlGamepad, pb->id);

        // Normalize axis value to [0, 1]
        if (type == kInputTypeAxisPlus)
          value = axis * (1.0f / 32767.0f);
        else
          value = axis * (1.0f / -32768.0f);

#if defined(__ANDROID__) || defined(__IOS__)
        // Inject Virtual Gamepad Axis
        if (gamepadNum == 0 && !gUserPrefersGamepad) {
          if (pb->id == SDL_GAMEPAD_AXIS_LEFTX) {
            float v = gVirtualInput.stickX;
            if (type == kInputTypeAxisPlus && v > 0)
              value = SDL_max(value, v);
            else if (type == kInputTypeAxisMinus && v < 0)
              value = SDL_max(value, -v);
          } else if (pb->id == SDL_GAMEPAD_AXIS_LEFTY) {
            float v = gVirtualInput.stickY;
            if (type == kInputTypeAxisPlus && v > 0)
              value = SDL_max(value, v);
            else if (type == kInputTypeAxisMinus && v < 0)
              value = SDL_max(value, -v);
          }
        }
#endif

        // Avoid magnitude bump when thumbstick is pushed past dead zone:
        // Bring magnitude from [kJoystickDeadZoneFrac, 1.0] to [0.0, 1.0].
        value = (value - deadZoneFrac) / (1.0f - deadZoneFrac);
        value = SDL_max(0, value); // clamp to 0 if within dead zone

#if _DEBUG
        GAME_ASSERT(value >= 0);
        GAME_ASSERT(value <= 1);
#endif

        actuation = SDL_max(actuation, value);
      }
    }

    gamepad->needAnalog[needNum] = actuation;

    UpdateKeyState(&gamepad->needStates[needNum], actuation >= .5f);
  }
}

#pragma mark -

#if defined(__ANDROID__) || defined(__IOS__)
static void SimulateKey(SDL_Scancode key, bool down) {
  SDL_Event event;
  SDL_memset(&event, 0, sizeof(event));
  event.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
  event.key.scancode = key;
  event.key.down = down; // SDL3 uses 'bool down'
  SDL_PushEvent(&event);
}

static SDL_Scancode GetMenuKeyFromTouch(float x, float y) {
  if (y < 0.25f)
    return SDL_SCANCODE_UP;
  if (y > 0.75f)
    return SDL_SCANCODE_DOWN;
  if (x < 0.2f)
    return SDL_SCANCODE_ESCAPE;
  return SDL_SCANCODE_RETURN;
}
#endif

/**********************/
/* PUBLIC FUNCTIONS   */
/**********************/

void DoSDLMaintenance(void) {
  gTextInput[0] = '\0';
  gMouseMotionNow = false;
  int mouseWheelDelta = 0;

#if defined(__ANDROID__) || defined(__IOS__)
  InitMobileInput();
#endif

  /**********************/
  /* DO SDL MAINTENANCE */
  /**********************/

  SDL_PumpEvents();
  SDL_Event event;
  while (SDL_PollEvent(&event)) {
    switch (event.type) {
    case SDL_EVENT_QUIT:
      CleanQuit(); // throws Pomme::QuitRequest
      return;

#if defined(__ANDROID__) || defined(__IOS__)
    case SDL_EVENT_FINGER_DOWN:
    case SDL_EVENT_FINGER_MOTION: {
      gUserPrefersGamepad = false; // Touch Reactivation
      for (int i = 0; i < MAX_TOUCH_FINGERS; i++) {
        if (!gFingers[i].active || gFingers[i].id == event.tfinger.fingerID) {
          gFingers[i].id = event.tfinger.fingerID;
          gFingers[i].x = event.tfinger.x;
          gFingers[i].y = event.tfinger.y;
          gFingers[i].active = true;
          break;
        }
      }
      UpdateVirtualGamepad();

      // Menu navigation (only if not handled by gamepad logic? or let both
      // work)
      if (event.type == SDL_EVENT_FINGER_DOWN && !gIsInGame) {
        SimulateKey(GetMenuKeyFromTouch(event.tfinger.x, event.tfinger.y),
                    true);
      }
      break;
    }

    case SDL_EVENT_FINGER_UP:
    case SDL_EVENT_FINGER_CANCELED: {
      for (int i = 0; i < MAX_TOUCH_FINGERS; i++) {
        if (gFingers[i].active && gFingers[i].id == event.tfinger.fingerID) {
          gFingers[i].active = false;
          if (gJoystickFingerActive &&
              gJoystickFingerID == event.tfinger.fingerID) {
            gJoystickFingerActive = false;
          }
          break;
        }
      }
      UpdateVirtualGamepad();

      if (!gIsInGame) {
        SimulateKey(GetMenuKeyFromTouch(event.tfinger.x, event.tfinger.y),
                    false);
      }
      break;
    }
#endif

    case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
      CleanQuit(); // throws Pomme::QuitRequest
      return;

    case SDL_EVENT_WINDOW_RESIZED:
      // QD3D_OnWindowResized(event.window.data1, event.window.data2);
      break;

    case SDL_EVENT_TEXT_INPUT:
      SDL_snprintf(gTextInput, sizeof(gTextInput), "%s", event.text.text);
      break;

    case SDL_EVENT_MOUSE_MOTION:
      gMouseMotionNow = true;
      break;

    case SDL_EVENT_MOUSE_WHEEL:
      mouseWheelDelta += event.wheel.y;
      mouseWheelDelta += event.wheel.x;
      break;

    case SDL_EVENT_GAMEPAD_ADDED:
      TryOpenGamepadFromJoystick(event.gdevice.which);
      break;

    case SDL_EVENT_GAMEPAD_REMOVED:
      OnJoystickRemoved(event.gdevice.which);
      break;

    case SDL_EVENT_GAMEPAD_REMAPPED:
      SDL_Log("Gamepad device remapped! %d", event.gdevice.which);
      break;

    case SDL_EVENT_KEY_DOWN:
      gUserPrefersGamepad = false;
      break;

    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
    case SDL_EVENT_GAMEPAD_BUTTON_UP: {
      bool isVirtual = false;
#if defined(__ANDROID__) || defined(__IOS__)
      if (event.gdevice.which == gVirtualJoystickID)
        isVirtual = true;
#endif
      if (!isVirtual)
        gUserPrefersGamepad = true;
      break;
    }

    case SDL_EVENT_GAMEPAD_AXIS_MOTION: {
      bool isVirtual = false;
#if defined(__ANDROID__) || defined(__IOS__)
      if (event.gaxis.which == gVirtualJoystickID)
        isVirtual = true;
#endif
      if (!isVirtual &&
          SDL_abs(event.gaxis.value) > 3000) // Deadzone check & ID check
        gUserPrefersGamepad = true;
      break;
    }
    }
  }

  // Refresh the state of each individual keyboard key
  UpdateRawKeyboardStates();

  // On ALT+ENTER, toggle fullscreen, and ignore ENTER until keyup
  ParseAltEnter();

  // Refresh the state of each mouse button
  UpdateMouseButtonStates(mouseWheelDelta);

  // Refresh the state of each input need
  UpdateInputNeeds();

  //-------------------------------------------------------------------------
  // Multiplayer gamepad input
  //-------------------------------------------------------------------------

  for (int gamepadNum = 0; gamepadNum < MAX_LOCAL_PLAYERS; gamepadNum++) {
    UpdateGamepadSpecificInputNeeds(gamepadNum);
  }

  /*******************/
  /* CHECK FOR CMD+Q */
  /*******************/
  // When in-game, take a different path (see PlayArea)

  if ((!gIsInGame || gSimulationPaused) && IsCmdQPressed()) {
    CleanQuit();
  }
}

#pragma mark -

Boolean GetKeyState(uint16_t sdlScancode) {
  if (sdlScancode >= SDL_SCANCODE_COUNT)
    return false;
  return 0 != (gKeyboardStates[sdlScancode] & KEYSTATE_ACTIVE_BIT);
}

Boolean GetNewKeyState(uint16_t sdlScancode) {
  if (sdlScancode >= SDL_SCANCODE_COUNT)
    return false;
  return gKeyboardStates[sdlScancode] == KEYSTATE_PRESSED;
}

#pragma mark -

Boolean GetClickState(int mouseButton) {
  if (mouseButton >= NUM_SUPPORTED_MOUSE_BUTTONS)
    return false;
  return 0 != (gMouseButtonStates[mouseButton] & KEYSTATE_ACTIVE_BIT);
}

Boolean GetNewClickState(int mouseButton) {
  if (mouseButton >= NUM_SUPPORTED_MOUSE_BUTTONS)
    return false;
  return gMouseButtonStates[mouseButton] == KEYSTATE_PRESSED;
}

#pragma mark -

Boolean GetNeedState(int needID, int playerID) {
  const Gamepad *gamepad = &gGamepads[playerID];

  GAME_ASSERT(playerID >= 0);
  GAME_ASSERT(playerID < MAX_LOCAL_PLAYERS);
  GAME_ASSERT(needID >= 0);
  GAME_ASSERT(needID < NUM_CONTROL_NEEDS);

  if (gamepad->open && (gamepad->needStates[needID] & KEYSTATE_ACTIVE_BIT)) {
    return true;
  }

  // Fallback to KB/M
  if (gNumLocalPlayers <= 1 || gamepad->fallbackToKeyboard) {
    return gNeedStates[needID] & KEYSTATE_ACTIVE_BIT;
  }

  return false;
}

Boolean GetNeedStateAnyP(int needID) {
  for (int i = 0; i < MAX_LOCAL_PLAYERS; i++) {
    if (gGamepads[i].open &&
        (gGamepads[i].needStates[needID] & KEYSTATE_ACTIVE_BIT)) {
      return true;
    }
  }

  // Fallback to KB/M
  return gNeedStates[needID] & KEYSTATE_ACTIVE_BIT;
}

Boolean GetNewNeedState(int needID, int playerID) {
  const Gamepad *gamepad = &gGamepads[playerID];

  GAME_ASSERT(playerID >= 0);
  GAME_ASSERT(playerID < MAX_LOCAL_PLAYERS);
  GAME_ASSERT(needID >= 0);
  GAME_ASSERT(needID < NUM_CONTROL_NEEDS);

  if (gamepad->open && gamepad->needStates[needID] == KEYSTATE_PRESSED) {
    return true;
  }

  // Fallback to KB/M
  if (gNumLocalPlayers <= 1 || gamepad->fallbackToKeyboard) {
    return gNeedStates[needID] == KEYSTATE_PRESSED;
  }

  return false;
}

Boolean GetNewNeedStateAnyP(int needID) {
  for (int i = 0; i < MAX_LOCAL_PLAYERS; i++) {
    if (gGamepads[i].open &&
        (gGamepads[i].needStates[needID] == KEYSTATE_PRESSED)) {
      return true;
    }
  }

  // Fallback to KB/M
  return gNeedStates[needID] == KEYSTATE_PRESSED;
}

static float GetAnalogValue(int needID, int playerID) {
  GAME_ASSERT(playerID >= 0);
  GAME_ASSERT(playerID < MAX_LOCAL_PLAYERS);
  GAME_ASSERT(needID >= 0);
  GAME_ASSERT(needID < NUM_CONTROL_NEEDS);

  const Gamepad *gamepad = &gGamepads[playerID];

  // Keyboard takes precedence when the key is pressed
  if ((gNumLocalPlayers <= 1 || gamepad->fallbackToKeyboard) &&
      gNeedStates[needID]) {
    return 1.0f;
  }

  if (gamepad->open) {
    return gamepad->needAnalog[needID];
  }

  return 0;
}

float GetNeedAxis1D(int negativeNeedID, int positiveNeedID, int playerID) {
  float neg = GetAnalogValue(negativeNeedID, playerID);
  float pos = GetAnalogValue(positiveNeedID, playerID);

  if (neg > pos) {
    return -neg;
  } else {
    return pos;
  }
}

#if 0
OGLPolar2D GetNeedAxis2D(int negXID, int posXID, int negYID, int posYID)
{
	float deadZone = negXID < NUM_REMAPPABLE_NEEDS ? kJoystickDeadZoneFrac : kJoystickDeadZoneFrac_UI;

	float negX = GetAnalogValue(negXID, true);
	float posX = GetAnalogValue(posXID, true);
	float x = negX > posX? -negX: posX;

	float negY = GetAnalogValue(negYID, true);
	float posY = GetAnalogValue(posYID, true);
	float y = negY > posY? -negY: posY;

	float mag = SDL_sqrtf(SQUARED(x) + SQUARED(y));
	if (mag < deadZone)
	{
		mag = 0;
	}
	else
	{
		mag = (mag - deadZone) / (1.0f - deadZone);
		mag = SDL_min(mag, 1);		// clamp to 0..1
	}

	float angle = SDL_atan2f(y, x);
	return (OGLPolar2D) { .a=angle, .r=mag };
}
#endif

Boolean UserWantsOut(void) {
  // kNeed_UIConfirm (A button) only for splash screens, NOT during gameplay
  // (A button conflicts with Forward/Throw gameplay inputs)
  if (!gIsInGame && GetNewNeedStateAnyP(kNeed_UIConfirm)) {
    return true;
  }
  // kNeed_UIBack (B button / Escape) - only exit when NOT in game
  // During gameplay, ESC should trigger pause (handled by schedulePause), not
  // exit
  if (!gIsInGame && GetNewNeedStateAnyP(kNeed_UIBack)) {
    return true;
  }
  // NOTE: kNeed_UIPause (Start button) removed - it should trigger pause dialog
  // (DoPauseDialog), not immediate game exit. Pause handling is in PlayArea via
  // schedulePause variable.
  //		|| GetNewClickState(SDL_BUTTON_LEFT)
  return false;
}

Boolean IsCmdQPressed(void) {
#if __APPLE__
  return (GetKeyState(SDL_SCANCODE_LGUI) || GetKeyState(SDL_SCANCODE_RGUI)) &&
         GetNewKeyState(SDL_GetScancodeFromKey(SDLK_Q, NULL));
#else
  // on non-mac systems, alt-f4 is handled by the system
  return false;
#endif
}

Boolean IsCheatKeyComboDown(void) {
  // The original Mac version used B-R-I, but some cheap PC keyboards can't
  // register this particular key combo, so C-M-R is available as an
  // alternative.
  return (GetKeyState(SDL_SCANCODE_B) && GetKeyState(SDL_SCANCODE_R) &&
          GetKeyState(SDL_SCANCODE_I)) ||
         (GetKeyState(SDL_SCANCODE_C) && GetKeyState(SDL_SCANCODE_M) &&
          GetKeyState(SDL_SCANCODE_R));
}

#pragma mark -

OGLVector2D GetAnalogSteering(int playerID) {
  return (OGLVector2D){
      .x = GetNeedAxis1D(kNeed_Left, kNeed_Right, playerID),
      .y = GetNeedAxis1D(kNeed_Forward, kNeed_Backward, playerID)};
}

#pragma mark -

/****************************** SDL JOYSTICK FUNCTIONS
 * ********************************/

int GetNumGamepads(void) {
  int count = 0;

#if 0
	for (int i = 0; i < SDL_NumJoysticks(); ++i)
	{
		if (SDL_IsGameController(i))
		{
			count++;
		}
	}
#else
  for (int i = 0; i < MAX_LOCAL_PLAYERS; i++) {
    if (gGamepads[i].open) {
      count++;
    }
  }
#endif

  return count;
}

SDL_Gamepad *GetGamepad(int n) {
  if (gGamepads[n].open) {
    return gGamepads[n].sdlGamepad;
  } else {
    return NULL;
  }
}

static int FindFreeGamepadSlot() {
  for (int i = 0; i < MAX_LOCAL_PLAYERS; i++) {
    if (!gGamepads[i].open) {
      return i;
    }
  }

  return -1;
}

static SDL_Gamepad *TryOpenGamepadFromJoystick(SDL_JoystickID joystickID) {
  int gamepadSlot = -1;

  // Check if already open
  for (int i = 0; i < MAX_LOCAL_PLAYERS; i++) {
    if (gGamepads[i].open &&
        SDL_GetGamepadID(gGamepads[i].sdlGamepad) == joystickID)
      return NULL;
  }

  // Check if we are opening the Virtual Gamepad
  bool isVirtual = false;
#if defined(__ANDROID__)
  isVirtual = (joystickID == gVirtualJoystickID);
#endif

  // Slot Allocation Logic
  if (isVirtual) {
    // Search backwards for a free slot
    for (int i = MAX_LOCAL_PLAYERS - 1; i >= 0; i--) {
      if (!gGamepads[i].open) {
        gamepadSlot = i;
        break;
      }
    }
  } else {
    // Physical Gamepad: Check for Virtual Hogging Slot 0
    bool hogging = false;
#if defined(__ANDROID__)
    if (gGamepads[0].open &&
        SDL_GetGamepadID(gGamepads[0].sdlGamepad) == gVirtualJoystickID)
      hogging = true;
#endif
    if (hogging) {
      SDL_Log("Physical Gamepad detected! Moving Virtual Gamepad from Slot 0 "
              "to make room...");

      // Find a new home for the Virtual Gamepad (high slot)
      int newVirtualSlot = -1;
      for (int i = MAX_LOCAL_PLAYERS - 1; i > 0; i--) {
        if (!gGamepads[i].open) {
          newVirtualSlot = i;
          break;
        }
      }

      if (newVirtualSlot > 0) {
        // Move the struct data
        gGamepads[newVirtualSlot] = gGamepads[0];

        // Zero out Slot 0 (but don't Close/Display message, just clear struct)
        SDL_memset(&gGamepads[0], 0, sizeof(Gamepad));

        SDL_Log("Virtual Gamepad moved to Slot %d", newVirtualSlot);
      }
    }

    // Standard "First Free" search
    gamepadSlot = FindFreeGamepadSlot();
  }

  if (gamepadSlot < 0) {
    SDL_Log("All gamepad slots used up.");
    return NULL;
  }

  // If we can't get an SDL_Gamepad from that joystick, don't bother
  if (!SDL_IsGamepad(joystickID)) {
    return NULL;
  }

  // Use this one
  SDL_Gamepad *sdlGamepad = SDL_OpenGamepad(joystickID);

  // Assign player ID
  SDL_SetGamepadPlayerIndex(sdlGamepad, gamepadSlot);

  gGamepads[gamepadSlot] = (Gamepad){
      .open = true,
      .sdlGamepad = sdlGamepad,
  };

  SDL_Log("Opened joystick %d as gamepad: %s\n", joystickID,
          SDL_GetGamepadName(gGamepads[gamepadSlot].sdlGamepad));

  return gGamepads[gamepadSlot].sdlGamepad;
}

static SDL_Gamepad *TryOpenAnyUnusedGamepad(bool showMessage) {
  int numJoysticks = 0;
  int numJoysticksAlreadyInUse = 0;

  SDL_JoystickID *joysticks = SDL_GetJoysticks(&numJoysticks);
  SDL_Gamepad *newGamepad = NULL;

  for (int i = 0; i < numJoysticks; ++i) {
    SDL_JoystickID joystickID = joysticks[i];

    // Usable as an SDL_Gamepad?
    if (!SDL_IsGamepad(joystickID)) {
      continue;
    }

    // Already in use?
    if (GetGamepadSlotFromJoystick(joystickID) >= 0) {
      numJoysticksAlreadyInUse++;
      continue;
    }

    // Use this one
    newGamepad = TryOpenGamepadFromJoystick(joystickID);
    if (newGamepad) {
      break;
    }
  }

  if (newGamepad) {
    // OK
  } else if (numJoysticksAlreadyInUse == numJoysticks) {
    // No-op; All joysticks already in use (or there might be zero joysticks)
  } else {
    SDL_Log("%d joysticks found, but none is suitable as an SDL_Gamepad.",
            numJoysticks);
    if (showMessage) {
      char messageBuf[1024];
      SDL_snprintf(messageBuf, sizeof(messageBuf),
                   "The game does not support your controller yet (\"%s\").\n\n"
                   "You can play with the keyboard and mouse instead. Sorry!",
                   SDL_GetJoystickNameForID(joysticks[0]));
      SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_WARNING,
                               "Controller not supported", messageBuf,
                               gSDLWindow);
    }
  }

  SDL_free(joysticks);

  return newGamepad;
}

void Rumble(float strength, uint32_t ms) {
#if 0 // TODO: Rumble for specific player
	if (NULL == gSDLController || !gGamePrefs.gamepadRumble)
		return;

	SDL_GamepadRumble(gSDLController, (Uint16)(strength * 65535), (Uint16)(strength * 65535), ms);
#endif
}

static int GetGamepadSlotFromJoystick(SDL_JoystickID joystickID) {
  for (int i = 0; i < MAX_LOCAL_PLAYERS; i++) {
    if (gGamepads[i].open &&
        SDL_GetGamepadID(gGamepads[i].sdlGamepad) == joystickID) {
      return i;
    }
  }

  return -1;
}

static void CloseGamepad(int gamepadSlot) {
  GAME_ASSERT(gGamepads[gamepadSlot].open);
  GAME_ASSERT(gGamepads[gamepadSlot].sdlGamepad);

  SDL_CloseGamepad(gGamepads[gamepadSlot].sdlGamepad);
  gGamepads[gamepadSlot].open = false;
  gGamepads[gamepadSlot].sdlGamepad = NULL;
}

static void MoveGamepad(int oldSlot, int newSlot) {
  if (oldSlot == newSlot)
    return;

  SDL_Log("Remapped player gamepad %d ---> %d", oldSlot, newSlot);

  gGamepads[newSlot] = gGamepads[oldSlot];

  // TODO: Does this actually work??
  if (gGamepads[newSlot].open) {
    SDL_SetGamepadPlayerIndex(gGamepads[newSlot].sdlGamepad, newSlot);
  }

  // Clear duplicate slot so we don't read it by mistake in the future
  gGamepads[oldSlot].open = false;
  gGamepads[oldSlot].sdlGamepad = NULL;
}

static void CompactGamepadSlots(void) {
  int writeIndex = 0;

  for (int i = 0; i < MAX_LOCAL_PLAYERS; i++) {
    GAME_ASSERT(writeIndex <= i);

    if (gGamepads[i].open) {
      MoveGamepad(i, writeIndex);
      writeIndex++;
    }
  }
}

static void TryFillUpVacantGamepadSlots(void) {
  while (TryOpenAnyUnusedGamepad(false) != NULL) {
    // Successful; there might be more joysticks available, keep going
  }
}

static void OnJoystickRemoved(SDL_JoystickID joystickID) {
  int gamepadSlot = GetGamepadSlotFromJoystick(joystickID);

  if (gamepadSlot >= 0) // we're using this joystick
  {
    SDL_Log("Joystick %d was removed, was used by gamepad slot #%d", joystickID,
            gamepadSlot);

    // Nuke reference to this gamepad
    CloseGamepad(gamepadSlot);
  }

  if (!gPlayerGamepadMappingLocked) {
    CompactGamepadSlots();
  }

  // Fill up any gamepad slots that are vacant
  TryFillUpVacantGamepadSlots();
}

void LockPlayerGamepadMapping(void) {
  int keyboardPlayer = gNumLocalPlayers - 1;

  for (int i = 0; i < MAX_LOCAL_PLAYERS; i++) {
    gGamepads[i].fallbackToKeyboard = (i == keyboardPlayer);
  }

  gPlayerGamepadMappingLocked = true;
}

void UnlockPlayerGamepadMapping(void) {
  gPlayerGamepadMappingLocked = false;
  CompactGamepadSlots();
  TryFillUpVacantGamepadSlots();
}

const char *GetPlayerName(int whichPlayer) {
  static char playerName[64];

  SDL_snprintf(playerName, sizeof(playerName), "%s %d", Localize(STR_PLAYER),
               whichPlayer + 1);

  return playerName;
}

const char *GetPlayerNameWithInputDeviceHint(int whichPlayer) {
  static char playerName[128];

  playerName[0] = '\0';

  snprintfcat(playerName, sizeof(playerName), "%s %d", Localize(STR_PLAYER),
              whichPlayer + 1);

  if (gGameMode == GAME_MODE_CAPTUREFLAG) {
    snprintfcat(playerName, sizeof(playerName), ", %s",
                Localize(gPlayerInfo[whichPlayer].team == 0 ? STR_RED_TEAM
                                                            : STR_GREEN_TEAM));
  }

  bool enoughGamepads = GetNumGamepads() >= gNumLocalPlayers;

  if (!enoughGamepads) {
    bool hasGamepad = gGamepads[whichPlayer].open;
    snprintfcat(playerName, sizeof(playerName), "\n[%s]",
                Localize(hasGamepad ? STR_GAMEPAD : STR_KEYBOARD));
  }

  return playerName;
}

#pragma mark -

void ResetDefaultKeyboardBindings(void) {
  for (int i = 0; i < NUM_CONTROL_NEEDS; i++) {
    SDL_memcpy(gGamePrefs.bindings[i].key, kDefaultInputBindings[i].key,
               sizeof(gGamePrefs.bindings[i].key));
  }
}

void ResetDefaultGamepadBindings(void) {
  for (int i = 0; i < NUM_CONTROL_NEEDS; i++) {
    SDL_memcpy(gGamePrefs.bindings[i].pad, kDefaultInputBindings[i].pad,
               sizeof(gGamePrefs.bindings[i].pad));
  }
}

void ResetDefaultMouseBindings(void) {
  for (int i = 0; i < NUM_CONTROL_NEEDS; i++) {
    gGamePrefs.bindings[i].mouseButton = kDefaultInputBindings[i].mouseButton;
  }
}

#if defined(__ANDROID__) || defined(__IOS__)
void DrawVirtualGamepad(void) {
  // Check if assets are loaded and if we are in the overlay pass
  if (gAtlases[SPRITE_GROUP_GAMEPAD] == NULL || !gDrawingOverlayPane)
    return;

  // Hide if user prefers physical gamepad
  if (gUserPrefersGamepad)
    return;

  int paneNum = GetOverlayPaneNumber();
  float lw = gGameView->panes[paneNum].logicalWidth;
  float lh = gGameView->panes[paneNum].logicalHeight;

  // Set 2D projection for overlay
  OGL_PushState();
  OGL_SetProjection(kProjectionType2DOrthoCentered);
  glDisable(GL_DEPTH_TEST);
  glDepthMask(GL_FALSE);
  glDisable(GL_CULL_FACE);
  glDisable(GL_LIGHTING);
  glDisable(GL_DITHER);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glDisable(GL_ALPHA_TEST);
  glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

  gGlobalTransparency = 0.5f;

  unsigned long flags = kTextMeshKeepCurrentProjection;

  // Use gVirtualInput for visual feedback (synced with sticky joystick logic)
  float stickX = gVirtualInput.stickX;
  float stickY = gVirtualInput.stickY;
  bool btnA = gVirtualInput.btnA;
  bool btnB = gVirtualInput.btnB;
  bool btnX = gVirtualInput.btnX;
  bool btnY = gVirtualInput.btnY;
  bool btnStart = gVirtualInput.btnStart;

  // Stick (centered at normalized 0.27, 0.85)
  float sx = (-0.5f + 0.27f) * lw;
  float sy = (-0.5f + 0.85f) * lh;
  DrawSprite2(SPRITE_GROUP_GAMEPAD, GAMEPAD_SObjType_StickBase, sx, sy, 0.3f,
              0.3f, 0, flags);
  DrawSprite2(SPRITE_GROUP_GAMEPAD, GAMEPAD_SObjType_StickNub, sx + stickX * 40,
              sy + stickY * 40, 0.4f, 0.4f, 0, flags);

  // Buttons (centered at normalized 0.85, 0.75)
  float bx = (-0.5f + 0.85f) * lw;
  float by = (-0.5f + 0.75f) * lh;
  float bsp = 55.0f;

  gGlobalColorFilter =
      btnA ? (OGLColorRGB){0.5f, 1.0f, 0.5f} : (OGLColorRGB){1, 1, 1};
  DrawSprite2(SPRITE_GROUP_GAMEPAD, GAMEPAD_SObjType_ButtonA, bx, by + bsp,
              0.3f, 0.3f, 0, flags);

  gGlobalColorFilter =
      btnB ? (OGLColorRGB){1.0f, 0.5f, 0.5f} : (OGLColorRGB){1, 1, 1};
  DrawSprite2(SPRITE_GROUP_GAMEPAD, GAMEPAD_SObjType_ButtonB, bx + bsp, by,
              0.3f, 0.3f, 0, flags);

  gGlobalColorFilter =
      btnX ? (OGLColorRGB){0.5f, 0.5f, 1.0f} : (OGLColorRGB){1, 1, 1};
  DrawSprite2(SPRITE_GROUP_GAMEPAD, GAMEPAD_SObjType_ButtonX, bx - bsp, by,
              0.3f, 0.3f, 0, flags);

  gGlobalColorFilter =
      btnY ? (OGLColorRGB){1.0f, 1.0f, 0.5f} : (OGLColorRGB){1, 1, 1};
  DrawSprite2(SPRITE_GROUP_GAMEPAD, GAMEPAD_SObjType_ButtonY, bx, by - bsp,
              0.3f, 0.3f, 0, flags);

  gGlobalColorFilter =
      btnStart ? (OGLColorRGB){0.8f, 0.8f, 0.8f} : (OGLColorRGB){1, 1, 1};
  // Start button: X aligned with Y button + offset right, Y at 0.15
  DrawSprite2(SPRITE_GROUP_GAMEPAD, GAMEPAD_SObjType_ButtonStart, bx + 30,
              (-0.5f + 0.15f) * lh, 0.3f, 0.3f, 0, flags);

  gGlobalColorFilter = (OGLColorRGB){1, 1, 1};
  gGlobalTransparency = 1.0f;
  glDisable(GL_BLEND);
  glDepthMask(GL_TRUE);
  OGL_PopState();
}
#else
void DrawVirtualGamepad(void) {}
#endif
