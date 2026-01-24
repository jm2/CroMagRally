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

// Touch Controls (enabled on all platforms, hidden until touch event)
#if defined(__ANDROID__) || (defined(__IOS__) && !defined(__TVOS__))
static SDL_Sensor *gAccelerometer = NULL;
#endif

// Touch Debugging
#define TOUCH_DEBUG_LINES 0

// Virtual joystick configuration (normalized screen coordinates)
#define STICK_VISUAL_CENTER_X 0.27f   // 27% from left edge
#define STICK_INPUT_CENTER_X 0.20f   // 27% from left edge
#define STICK_VISUAL_CENTER_Y 0.85f // Visual position (near bottom)
#define STICK_INPUT_CENTER_Y  0.68f // Input logical position (closer to visual 0.85)
#define STICK_RADIUS_X 0.16f   // Horizontal radius (Increased for lower sensitivity)
#define STICK_RADIUS_Y 0.16f   // Vertical radius (Increased for lower sensitivity)
#define STICK_VISUAL_RADIUS_X 0.10f // Visual radius (Decoupled from input range)
#define STICK_VISUAL_RADIUS_Y 0.10f // Visual radius (Decoupled from input range)
#define STICK_CLAIM_RADIUS 0.20f  // Touch claim radius (Increased for better responsiveness)

// Button layout configuration
#define BUTTON_CENTER_X 0.85f
#define BUTTON_CENTER_Y 0.78f
#define BUTTON_TOUCH_RADIUS 0.18f
// Button input tuning
// Button input tuning
// Visual spacing is +/- 55 pixels from center.
#define BUTTON_DEADZONE_X 0.00f 
#define BUTTON_DEADZONE_Y 0.00f
#define BUTTON_INPUT_OFFSET_X -0.03f  // Shift touch area right to match visual
#define BUTTON_INPUT_OFFSET_Y -0.06f  // Shift touch area down to match visual

// Start Button layout
#define START_BUTTON_CENTER_X 0.88f
#define START_BUTTON_CENTER_Y 0.14f
#define START_BUTTON_WIDTH 0.12f
#define START_BUTTON_HEIGHT 0.12f
#define START_BUTTON_INPUT_OFFSET_X -0.06f
#define START_BUTTON_INPUT_OFFSET_Y -0.06f

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
static bool gTouchControlsActive = false;  // Only true after real touch event

typedef struct {
  float stickX, stickY;
  float visualStickX, visualStickY; // Separate visual state for menu bouncing fix
  bool btnA, btnB, btnX, btnY, btnStart;
} VirtualInputState;
static VirtualInputState gVirtualInput = {0};

#pragma mark -
/**********************/

// Initialize virtual touch gamepad (all platforms)
// Initialize input data structures (reset stale state)
static void ResetTouchInput(void) {
  gJoystickFingerActive = false;
  gJoystickFingerID = 0;
  for (int i = 0; i < MAX_TOUCH_FINGERS; i++) {
    gFingers[i].active = false;
  }
}

static void InitTouchData(void) {
  // Check for stale static state (Android process reuse)
  if (gVirtualJoystickID != 0 && gVirtualJoystick != NULL) {
    if (SDL_GetJoystickFromID(gVirtualJoystickID) == NULL) {
      SDL_Log("Detected stale Virtual Joystick ID! Resetting input state.");
      gVirtualJoystickID = 0;
      gVirtualJoystick = NULL;
      ResetTouchInput(); 
    }
  }
}

// Create virtual joystick if not exists (Lazy Load)
static void EnableVirtualJoystick(void) {
  if (gVirtualJoystickID) return;

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
    gTouchControlsActive = true; 
  } else {
    SDL_Log("Failed to add Virtual Gamepad: %s", SDL_GetError());
  }
}

#if defined(__ANDROID__) || (defined(__IOS__) && !defined(__TVOS__))
static void InitMobileInput(void) {
  InitTouchData();

  if (!gAccelerometer) {
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
}
#endif

static void UpdateVirtualGamepad(void) {
  if (!gVirtualJoystick)
    return;

  float targetStickX = 0, targetStickY = 0;
  bool btnA = false, btnB = false, btnX = false, btnY = false, btnStart = false;

  // Validate joystick finger - if it's marked active but the finger isn't found,
  // reset it (fixes stuck joystick bug)
  if (gJoystickFingerActive) {
    bool fingerFound = false;
    for (int i = 0; i < MAX_TOUCH_FINGERS; i++) {
      if (gFingers[i].active && gFingers[i].id == gJoystickFingerID) {
        fingerFound = true;
        break;
      }
    }
    if (!fingerFound) {
      gJoystickFingerActive = false;
      gJoystickFingerID = 0;
    }
  }

  // Process touch input to populate gVirtualInput for Input Injection.
  int w, h;
  SDL_GetWindowSize(gSDLWindow, &w, &h);
  float aspect = (float)w / (float)h;

  for (int i = 0; i < MAX_TOUCH_FINGERS; i++) {
    if (!gFingers[i].active)
      continue;

    float x = gFingers[i].x;
    float y = gFingers[i].y;

    // Check for Sticky Joystick Ownership
    bool isJoystickFinger = false;
    if (gJoystickFingerActive && gJoystickFingerID == gFingers[i].id) {
      isJoystickFinger = true;
    } else if (!gJoystickFingerActive) {
      // Check if finger is within claim radius of joystick center
      // CORRECTED: Apply aspect ratio to X distance for circular area in screen space
      float dx = (x - STICK_INPUT_CENTER_X) * aspect;
      float dy = y - STICK_INPUT_CENTER_Y;
      float dist = SDL_sqrtf(dx * dx + dy * dy);
      if (dist < STICK_CLAIM_RADIUS) { // Radius defined in Y-normalized units (relative to height)
        // New claim!
        gJoystickFingerActive = true;
        gJoystickFingerID = gFingers[i].id;
        isJoystickFinger = true;
      }
    }
    
    // Validate joystick finger - if it's marked active but the finger isn't found,
    // reset it (fixes stuck joystick bug)
    // MOVED CHECK INSIDE LOOP? No, the check was at the top. Let's update the top check too.

    if (isJoystickFinger) {
      // Calculate normalized stick deflection with aspect correction
      // We want STICK_RADIUS_Y to represent the physical radius circle
      float dx = (x - STICK_INPUT_CENTER_X) * aspect; 
      float dy = y - STICK_INPUT_CENTER_Y;
      
      // Feature request: Reset to center if sliding out of claim area.
      float dist = SDL_sqrtf(dx * dx + dy * dy);
      if (dist > STICK_CLAIM_RADIUS) {
         gJoystickFingerActive = false;
         gJoystickFingerID = 0;
         // Input will fall through to 0 on next frame logic (since active is false)
         continue; 
      }

      // Normalize
      dx /= STICK_RADIUS_Y; // Use Y radius for both for circular sensitivity
      dy /= STICK_RADIUS_Y;

      // Clamp each axis to [-1, 1] independently (or circular clamp?)
      // Original logic clamped independently. Let's keep that but now inputs are circular-ish.
      if (dx < -1.0f) dx = -1.0f;
      if (dx > 1.0f) dx = 1.0f;
      if (dy < -1.0f) dy = -1.0f;
      if (dy > 1.0f) dy = 1.0f;
      targetStickX = dx;
      targetStickY = dy;
    }
    // Right side: Buttons
    else if (x > 0.6f && y > 0.5f) {
      // Use offset constants for tuning
      float btnCenterX = BUTTON_CENTER_X + BUTTON_INPUT_OFFSET_X;
      float btnCenterY = BUTTON_CENTER_Y + BUTTON_INPUT_OFFSET_Y;
      
      // Aspect correct the button distance check too?
      // The button cluster is somewhat oval if we don't. 
      // The limits (0.03) are hardcoded.
      // Let's apply aspect to distance check for the "Outer Ring"
      float dx = (x - btnCenterX) * aspect;
      float dy = y - btnCenterY;
      float distSq = dx * dx + dy * dy;
      
      // BUTTON_TOUCH_RADIUS is 0.18f.
      if (distSq < BUTTON_TOUCH_RADIUS * BUTTON_TOUCH_RADIUS) {
         // Re-calculate raw dx/dy for the internal quadrant checks 
         // Note: if we aspect correct the outer ring, we should probably aspect correct the inner checks too 
         // so the X split is physically consistent.
         float rawDx = x - btnCenterX;
         
         // Quadrant check with Diagonal Split
         // Aspect correct the X distance so the split is a true 45-degree X on screen.
         bool vertical = SDL_fabs(dy) > SDL_fabs(rawDx * aspect);
         
         if (vertical) {
            if (dy > BUTTON_DEADZONE_Y) btnA = true;
            else if (dy < -BUTTON_DEADZONE_Y) btnY = true;
         } else {
            if (rawDx > BUTTON_DEADZONE_X) btnB = true;
            else if (rawDx < -BUTTON_DEADZONE_X) btnX = true;
         }
      }
    }
    // Top Right: Start (Pause) - Rectangular Check
    else if (x > 0.8f && y < 0.35f) { // Coarse check
       float startCenterX = START_BUTTON_CENTER_X + START_BUTTON_INPUT_OFFSET_X;
       float startCenterY = START_BUTTON_CENTER_Y + START_BUTTON_INPUT_OFFSET_Y;
       
       float halfW = START_BUTTON_WIDTH / 2.0f;
       float halfH = START_BUTTON_HEIGHT / 2.0f;
       
       if (SDL_fabs(x - startCenterX) < halfW &&
           SDL_fabs(y - startCenterY) < halfH) {
          btnStart = true;
       }
    }
  }

  // Update Global Virtual Input State (for Injection into Player 1)
  // Smoothing
  if (gJoystickFingerActive) {
    gVirtualInput.stickX = gVirtualInput.stickX * 0.5f + targetStickX * 0.5f;
    gVirtualInput.stickY = gVirtualInput.stickY * 0.5f + targetStickY * 0.5f;
    
    // Snap close-to-zero values to clean up final rest
    if (SDL_fabs(gVirtualInput.stickX) < 0.01f) gVirtualInput.stickX = 0;
    if (SDL_fabs(gVirtualInput.stickY) < 0.01f) gVirtualInput.stickY = 0;
    
    // CAPTURE VISUAL STATE HERE BEFORE MENU REPEAT LOGIC
    gVirtualInput.visualStickX = gVirtualInput.stickX;
    gVirtualInput.visualStickY = gVirtualInput.stickY;
    
    // Auto-repeat for menu navigation (when not in game)
    // Simulates repeated presses when holding the stick up/down
    if (!gIsInGame) {
      static uint32_t sStickHoldStartTime = 0;
      static bool sStickHeld = false;
      
      if (SDL_fabs(targetStickY) > 0.5f) {
        uint32_t now = SDL_GetTicks();
        if (!sStickHeld) {
          sStickHeld = true;
          sStickHoldStartTime = now;
        } else {
          uint32_t heldTime = now - sStickHoldStartTime;
          // Initial delay 400ms
          if (heldTime > 400) {
            uint32_t repeatTime = heldTime - 400;
            // Accelerate: Slow repeat (250ms) for first 1s, then Fast (100ms)
            uint32_t rate = (repeatTime > 1000) ? 100 : 250;
            
            // Create a 50ms "gap" (return to 0) to trigger a new press event
            if ((repeatTime % rate) < 50) {
              gVirtualInput.stickY = 0;
            }
          }
        }
      } else {
        sStickHeld = false;
      }
    }
  } else {
    gVirtualInput.stickX = 0;
    gVirtualInput.stickY = 0;
    gVirtualInput.visualStickX = 0;
    gVirtualInput.visualStickY = 0;
  }
  
  gVirtualInput.btnA = btnA;
  gVirtualInput.btnB = btnB;
  gVirtualInput.btnX = btnX;
  gVirtualInput.btnY = btnY;
  gVirtualInput.btnStart = btnStart;

  // Only send to SDL Virtual Joystick device when NOT using physical gamepad.
  // This prevents the virtual device from conflicting with the physical one.
  if (!gUserPrefersGamepad) {
    SDL_SetJoystickVirtualAxis(gVirtualJoystick, SDL_GAMEPAD_AXIS_LEFTX,
                               (int16_t)(gVirtualInput.stickX * 32767));
    SDL_SetJoystickVirtualAxis(gVirtualJoystick, SDL_GAMEPAD_AXIS_LEFTY,
                               (int16_t)(gVirtualInput.stickY * 32767));
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
      } else if (type == kInputTypeAxisPlus || type == kInputTypeAxisMinus) {
        float value;
        int16_t axis = SDL_GetGamepadAxis(sdlGamepad, pb->id);

        // Normalize axis value to [0, 1]
        if (type == kInputTypeAxisPlus)
          value = axis * (1.0f / 32767.0f);
        else
          value = axis * (1.0f / -32768.0f);

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

/**********************/
/* PUBLIC FUNCTIONS   */
/**********************/

void DoSDLMaintenance(void) {
  gTextInput[0] = '\0';
  gMouseMotionNow = false;
  int mouseWheelDelta = 0;

  // Initialize touch input data (but don't create joystick yet on desktop)
  InitTouchData();
#if defined(__ANDROID__) || (defined(__IOS__) && !defined(__TVOS__))
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

    // Touch input handling (all platforms - hidden until activated by real touch)
    case SDL_EVENT_FINGER_DOWN:
    case SDL_EVENT_FINGER_MOTION: {
      // Only activate touch controls from real touch events (not mouse-simulated)
      if (event.tfinger.touchID == SDL_TOUCH_MOUSEID) break;

      if (!gTouchControlsActive) {
         EnableVirtualJoystick(); // Lazy create
         gTouchControlsActive = true;  // Show touch controls
      }
      gUserPrefersGamepad = false; // Touch Reactivation
      
      int foundSlot = -1;
      // 1. Search for existing finger with this ID
      for (int i = 0; i < MAX_TOUCH_FINGERS; i++) {
         if (gFingers[i].active && gFingers[i].id == event.tfinger.fingerID) {
            foundSlot = i;
            break;
         }
      }
      // 2. If not found, search for empty slot
      if (foundSlot == -1) {
         for (int i = 0; i < MAX_TOUCH_FINGERS; i++) {
            if (!gFingers[i].active) {
               foundSlot = i;
               break;
            }
         }
      }
      
      if (foundSlot != -1) {
          gFingers[foundSlot].id = event.tfinger.fingerID;
          gFingers[foundSlot].x = event.tfinger.x;
          gFingers[foundSlot].y = event.tfinger.y;
          gFingers[foundSlot].active = true;
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
      break;
    }

    case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
      CleanQuit(); // throws Pomme::QuitRequest
      return;

    case SDL_EVENT_WINDOW_RESIZED:
      // QD3D_OnWindowResized(event.window.data1, event.window.data2);
      break;
      
    case SDL_EVENT_WINDOW_FOCUS_LOST:
    case SDL_EVENT_WINDOW_MINIMIZED:
    case SDL_EVENT_DID_ENTER_BACKGROUND:
      // Reset touch state to prevents buttons/joystick getting stuck
      ResetTouchInput();
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
      if (event.gdevice.which == gVirtualJoystickID)
        isVirtual = true;
      if (!isVirtual)
        gUserPrefersGamepad = true;
      break;
    }

    case SDL_EVENT_GAMEPAD_AXIS_MOTION: {
      bool isVirtual = false;
      if (event.gaxis.which == gVirtualJoystickID)
        isVirtual = true;
      
      if (!isVirtual &&
          SDL_abs(event.gaxis.value) > 3000) { // Deadzone check & ID check
        gUserPrefersGamepad = true;
      }
      break;
    }
    }
  }

  // Update virtual gamepad every frame (handles smoothing/auto-repeat)
  UpdateVirtualGamepad();

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

Boolean IsAnyNewInput(void)
{
	// Check any key press
	for (int i = 0; i < SDL_SCANCODE_COUNT; i++)
	{
		if (GetNewKeyState(i))
			return true;
	}

	// Check any mouse click
	for (int i = 1; i < NUM_SUPPORTED_MOUSE_BUTTONS_PURESDL; i++)
	{
		if (GetNewClickState(i))
			return true;
	}

	// Check any gamepad button
	for (int i = 0; i < MAX_LOCAL_PLAYERS; i++)
	{
		if (gGamepads[i].open && gGamepads[i].sdlGamepad)
		{
			SDL_Gamepad* gamepad = gGamepads[i].sdlGamepad;
			
			for (int btn = 0; btn < SDL_GAMEPAD_BUTTON_COUNT; btn++)
			{
				if (SDL_GetGamepadButton(gamepad, btn))
					return true;
			}
			
			// Optional: Check axis movement (threshold > 0.5 to coincide with digital input feel)
			for (int axis = 0; axis < SDL_GAMEPAD_AXIS_COUNT; axis++)
			{
				if (SDL_abs(SDL_GetGamepadAxis(gamepad, axis)) > 16000)
					return true;
			}
		}
	}

	return false;
}

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
  bool isVirtual = (joystickID == gVirtualJoystickID);

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
    if (gGamepads[0].open &&
        SDL_GetGamepadID(gGamepads[0].sdlGamepad) == gVirtualJoystickID)
      hogging = true;
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

// Debug helpers implemented inline below to avoid unused function warnings
// (Cleaned up unused static functions)

// Virtual gamepad rendering (all platforms, shown when touch is activated)
void DrawVirtualGamepad(void) {
  // Check if assets are loaded and if we are in the overlay pass
  if (gAtlases[SPRITE_GROUP_GAMEPAD] == NULL || !gDrawingOverlayPane)
    return;

#if defined(__TVOS__)
  return; // Never draw touch interface on tvOS
#endif

  // Hide if touch controls not activated or user prefers physical gamepad
  if (!gTouchControlsActive || gUserPrefersGamepad) {
    if (gTouchControlsActive && gUserPrefersGamepad) {
       static int sLogThrottle = 0;
    }
    return;
  }

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
  // Use VISUAL stick state to prevent bounces
  float stickX = gVirtualInput.visualStickX;
  float stickY = gVirtualInput.visualStickY;
  bool btnA = gVirtualInput.btnA;
  bool btnB = gVirtualInput.btnB;
  bool btnX = gVirtualInput.btnX;
  bool btnY = gVirtualInput.btnY;
  bool btnStart = gVirtualInput.btnStart;

  // Stick position using STICK_* constants for consistency with input logic
  // Convert normalized coordinates to screen space (centered projection)
  float sx = (-0.5f + STICK_VISUAL_CENTER_X) * lw;
  float sy = (-0.5f + STICK_VISUAL_CENTER_Y) * lh;
  
  // Calculate visual nub displacement using separate X/Y radii for proper scaling
  float nubOffsetX = STICK_VISUAL_RADIUS_X * lw;
  float nubOffsetY = STICK_VISUAL_RADIUS_Y * lh;
  
  DrawSprite2(SPRITE_GROUP_GAMEPAD, GAMEPAD_SObjType_StickBase, sx, sy, 0.3f,
              0.3f, 0, flags);
  DrawSprite2(SPRITE_GROUP_GAMEPAD, GAMEPAD_SObjType_StickNub, 
              sx + stickX * nubOffsetX,
              sy + stickY * nubOffsetY, 0.4f, 0.4f, 0, flags);

  // Buttons (centered at normalized BUTTON_CENTER_X, BUTTON_CENTER_Y)
  float bx = (-0.5f + BUTTON_CENTER_X) * lw;
  float by = (-0.5f + BUTTON_CENTER_Y) * lh;
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

  // Start button: Fixed position
  float startX = (-0.5f + START_BUTTON_CENTER_X) * lw;
  float startY = (-0.5f + START_BUTTON_CENTER_Y) * lh;
  
  DrawSprite2(SPRITE_GROUP_GAMEPAD, GAMEPAD_SObjType_ButtonStart, startX,
              startY, 0.3f, 0.3f, 0, flags);

  gGlobalColorFilter = (OGLColorRGB){1, 1, 1};
  gGlobalTransparency = 1.0f;

#if TOUCH_DEBUG_LINES
    // DEBUG: Draw Input Catchment Areas
    // ---------------------------------
    // Joystick Input Center (Screen Space)
    float inputSx = (-0.5f + STICK_INPUT_CENTER_X) * lw;
    float inputSy = (-0.5f + STICK_INPUT_CENTER_Y) * lh;
    
    // Joystick Claim Radius (Yellow) - Now Aspect Corrected (Circular on Screen)
    glColor4f(1, 1, 0, 1);
    glDisable(GL_TEXTURE_2D);
    glBegin(GL_LINE_LOOP);
    const int segments = 32;
    for (int i = 0; i < segments; i++) {
        float angle = 2.0f * 3.14159f * i / segments;
        // STICK_CLAIM_RADIUS is normalized to Height (Y).
        // To draw circle in pixels: r_px = radius * lh.
        float r_px = STICK_CLAIM_RADIUS * lh;
        glVertex2f(inputSx + r_px * SDL_cosf(angle), 
                   inputSy + r_px * SDL_sinf(angle));
    }
    glEnd();
    glEnable(GL_TEXTURE_2D);

    // Joystick Output Range (Red) - STICK_RADIUS_Y * lh
    glColor4f(1, 0, 0, 1);
    glDisable(GL_TEXTURE_2D);
    glBegin(GL_LINE_LOOP);
    float outputR_px = STICK_RADIUS_Y * lh;
    for (int i = 0; i < segments; i++) {
        float angle = 2.0f * 3.14159f * i / segments;
        glVertex2f(inputSx + outputR_px * SDL_cosf(angle), 
                   inputSy + outputR_px * SDL_sinf(angle));
    }
    glEnd();
    glEnable(GL_TEXTURE_2D);

    // Button Areas (Green) - Outer Ring
    glColor4f(0, 1, 0, 1);
    float btnTouchX = (-0.5f + BUTTON_CENTER_X + BUTTON_INPUT_OFFSET_X) * lw;
    float btnTouchY = (-0.5f + BUTTON_CENTER_Y + BUTTON_INPUT_OFFSET_Y) * lh;
    
    glDisable(GL_TEXTURE_2D);
    glBegin(GL_LINE_LOOP);
    float btnR_px = BUTTON_TOUCH_RADIUS * lh; // Assuming normalized to height now for circle
    for (int i = 0; i < segments; i++) {
        float angle = 2.0f * 3.14159f * i / segments;
        glVertex2f(btnTouchX + btnR_px * SDL_cosf(angle), 
                   btnTouchY + btnR_px * SDL_sinf(angle));
    }
    glEnd();
    
    // Button Deadzones (Red Lines)
    glColor4f(1, 0, 0, 1);
    glBegin(GL_LINES);
    
    float splitX = BUTTON_DEADZONE_X * lw;
    float splitY = BUTTON_DEADZONE_Y * lh; 
    
    // Draw Center Deadzone Box (Collapsed to center if 0)
    // Removed per user request - just X lines now.

    // Draw Diagonal Separators (X-shape) extending to radius
    // These visualize the 'vertical vs horizontal' decision boundary (abs(dy) > abs(dx))
    // We draw them from the deadzone corners out to the circle edge roughly.
    // 0.707 is cos/sin 45 deg.
    float diagX = btnR_px * 0.707f;
    float diagY = btnR_px * 0.707f; // Use circle aspect? btnR_px is already scaled by lh.

    // If we assume a square aspect logic for the diagonal check (rawDx vs dy):
    // rawDx = x - center; dy = y - center. x,y are normalized?
    // Wait, rawDx is diff in normalized X units. dy is diff in normalized Y units.
    // If aspect ratio is involved (e.g. 16:9), then dx units != dy units spatially!
    // But the input logic compares raw normalized values: `abs(dy) > abs(rawDx)`.
    // So visually, the slope is 1.0 in *normalized space*.
    // In screen pixels: Px = Nx * lw. Py = Ny * lh.
    // So slope in pixels = (dy*lh) / (dx*lw) = (1*lh) / (1*lw) = lh/lw = 1/aspect.
    // So the line is NOT 45 degrees on screen. It points slightly flatter on wide screen.
    // Let's draw that exactly.
    
    float cornerX = btnR_px * (lh/lw); // Just project out far enough? 
    // Actually just use any point on the line y = x (normalized).
    // Point A: (0,0) -> (btnTouchX, btnTouchY).
    // Point B: (1,1) -> (btnTouchX + lw, btnTouchY + lh).
    // But we want to clip to radius or just draw 'long enough'.
    
    float dEnd = btnR_px; 
    // Draw true 45-degree diagonals (Squared X)
    // In pixel space -> dx = dy.
    // Normalized Y * lh = Pixel Y.
    // Normalized X * lw = Pixel X.
    // We want Pixel X = Pixel Y.
    // So Normalized X * lw = Normalized Y * lh => NormX = NormY * (lh/lw) = NormY / aspect.
    // Or simpler: just add the same pixel amount to CenterX_px and CenterY_px.
    
    float diagLen_px = btnR_px; // Length in pixels
    
    // Line 1: (+, +)
    glVertex2f(btnTouchX, btnTouchY);
    glVertex2f(btnTouchX + diagLen_px, btnTouchY + diagLen_px);
    
    // Line 2: (+, -)
    glVertex2f(btnTouchX, btnTouchY);
    glVertex2f(btnTouchX + diagLen_px, btnTouchY - diagLen_px);
    
    // Line 3: (-, +)
    glVertex2f(btnTouchX, btnTouchY);
    glVertex2f(btnTouchX - diagLen_px, btnTouchY + diagLen_px);
    
    // Line 4: (-, -)
    glVertex2f(btnTouchX, btnTouchY);
    glVertex2f(btnTouchX - diagLen_px, btnTouchY - diagLen_px);

    glEnd();

    glEnable(GL_TEXTURE_2D);
    
    // Start Button Area (Blue) - Rectangular
    glColor4f(0, 0, 1, 1);
    
    glDisable(GL_TEXTURE_2D);
    glBegin(GL_LINE_LOOP);
    float startHalfW_px = (START_BUTTON_WIDTH / 2.0f) * lw;
    float startHalfH_px = (START_BUTTON_HEIGHT / 2.0f) * lh;
    // START_BUTTON_CENTER X/Y are 0..1 normalized.
    // Convert center to pixels (centered at 0,0 for OpenGL if (-0.5..0.5) space used elsewhere?)
    // The other drawing code uses inputSx = (-0.5f + CENTER) * lw.
    float startCx_px = (-0.5f + START_BUTTON_CENTER_X + START_BUTTON_INPUT_OFFSET_X) * lw;
    float startCy_px = (-0.5f + START_BUTTON_CENTER_Y + START_BUTTON_INPUT_OFFSET_Y) * lh;
    
    glVertex2f(startCx_px - startHalfW_px, startCy_px - startHalfH_px);
    glVertex2f(startCx_px + startHalfW_px, startCy_px - startHalfH_px);
    glVertex2f(startCx_px + startHalfW_px, startCy_px + startHalfH_px);
    glVertex2f(startCx_px - startHalfW_px, startCy_px + startHalfH_px);
    glEnd();
    glEnable(GL_TEXTURE_2D);
    
    glColor4f(1, 1, 1, 1); // Reset
#endif

  glDisable(GL_BLEND);
  glDepthMask(GL_TRUE);
  OGL_PopState();
}
