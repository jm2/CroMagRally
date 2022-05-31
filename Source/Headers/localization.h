#pragma once

typedef enum GameLanguageID
{
	LANGUAGE_ILLEGAL = -1,
	LANGUAGE_ENGLISH = 0,
	LANGUAGE_FRENCH,
	LANGUAGE_GERMAN,
	LANGUAGE_SPANISH,
	LANGUAGE_ITALIAN,
	LANGUAGE_SWEDISH,
	NUM_LANGUAGES
} GameLanguageID;

typedef enum LocStrID
{
	STR_NULL					= 0,

	STR_LANGUAGE_NAME,

	STR_ENGLISH,
	STR_FRENCH,
	STR_GERMAN,
	STR_SPANISH,
	STR_ITALIAN,
	STR_SWEDISH,

	STR_NEW_GAME,
	STR_LOAD_GAME,
	STR_OPTIONS,
	STR_EXTRAS,
	STR_QUIT,
	STR_1PLAYER,
	STR_2PLAYER,
	STR_3PLAYER,
	STR_4PLAYER,
	STR_5PLAYER,
	STR_6PLAYER,
	STR_NET_GAME,
	STR_SETTINGS,
	STR_HELP,
	STR_CREDITS,
	STR_PHYSICS_EDITOR,
	STR_SCOREBOARD,
	STR_RACE,
	STR_TAG1,
	STR_TAG2,
	STR_SURVIVAL,
	STR_CAPTUREFLAG,
	STR_PRACTICE,
	STR_TOURNAMENT,
	STR_STONE_AGE,
	STR_BRONZE_AGE,
	STR_IRON_AGE,
	STR_AGE_COMPLETE,
	STR_TOURNAMENT_OBJECTIVE,
	STR_TOURNAMENT_OBJECTIVE_EASY,
	STR_OBJECTIVE_COMPLETE,
	STR_OBJECTIVE_INCOMPLETE,
	STR_HOST_NET_GAME,
	STR_JOIN_NET_GAME,
	STR_RESUME_GAME,
	STR_RETIRE_GAME,
	STR_QUIT_APPLICATION,
	STR_TRY_AGAIN,
	STR_RETIRE,
	STR_PRESS_ANY_KEY,
	STR_PRESS_SPACE,
	STR_PRESS_START,
	STR_PRESS_ESC_TO_GO_BACK,
	STR_1_TRY_REMAINING,
	STR_2_TRIES_REMAINING,
	STR_3_TRIES_REMAINING,
	STR_LAP,
	STR_LAP_2,
	STR_LAP_3,
	STR_PLAYER,
	STR_RED_TEAM,
	STR_GREEN_TEAM,
	STR_KEYBOARD,
	STR_GAMEPAD,
	STR_BROG,
	STR_GRAG,
	STR_LEVEL_1,
	STR_LEVEL_2,
	STR_LEVEL_3,
	STR_LEVEL_4,
	STR_LEVEL_5,
	STR_LEVEL_6,
	STR_LEVEL_7,
	STR_LEVEL_8,
	STR_LEVEL_9,
	STR_MPLEVEL_1,
	STR_MPLEVEL_2,
	STR_MPLEVEL_3,
	STR_MPLEVEL_4,
	STR_MPLEVEL_5,
	STR_MPLEVEL_6,
	STR_MPLEVEL_7,
	STR_MPLEVEL_8,
	STR_COMPLETE_STONE_AGE_TO_UNLOCK_TRACK,
	STR_COMPLETE_BRONZE_AGE_TO_UNLOCK_TRACK,
	STR_CAR_MODEL_1,
	STR_CAR_MODEL_2,
	STR_CAR_MODEL_3,
	STR_CAR_MODEL_4,
	STR_CAR_MODEL_5,
	STR_CAR_MODEL_6,
	STR_CAR_MODEL_7,
	STR_CAR_MODEL_8,
	STR_CAR_MODEL_9,
	STR_CAR_MODEL_10,
	STR_CAR_MODEL_11,
	STR_CAR_STAT_1,
	STR_CAR_STAT_2,
	STR_CAR_STAT_3,
	STR_CAR_STAT_4,
	STR_CAR_STAT_ABBREV_1,
	STR_CAR_STAT_ABBREV_2,
	STR_CAR_STAT_ABBREV_3,
	STR_CAR_STAT_ABBREV_4,
	STR_CAR_STAT_METER_0,
	STR_CAR_STAT_METER_1,
	STR_CAR_STAT_METER_2,
	STR_CAR_STAT_METER_3,
	STR_CAR_STAT_METER_4,
	STR_CAR_STAT_METER_5,
	STR_CAR_STAT_METER_6,
	STR_CAR_STAT_METER_7,
	STR_CAR_STAT_METER_8,
	STR_ELIMINATED,
	STR_YOU_WIN,
	STR_YOU_LOSE,
	STR_3RDPERSON_WINS,
	STR_YOUR_TIME,
	STR_BEST_TIME,
	STR_TOTAL_TIME,
	STR_NEW_RECORD,
	STR_POW_1,
	STR_POW_2,
	STR_POW_3,
	STR_POW_4,
	STR_POW_5,
	STR_POW_6,
	STR_POW_7,
	STR_POW_8,
	STR_POW_9,
	STR_POW_10,
	STR_POW_11,
	STR_POW_12,

	STR_LANGUAGE,
	STR_DIFFICULTY,
	STR_SIMPLISTIC,
	STR_EASY,
	STR_MEDIUM,
	STR_HARD,
	STR_DIFFICULTY_1,
	STR_DIFFICULTY_2,
	STR_DIFFICULTY_3,
	STR_DIFFICULTY_4,
	STR_KEEPAWAYTAG_HELP,
	STR_STAMPEDETAG_HELP,
	STR_TAG_DURATION,
	STR_2_MINUTES,
	STR_3_MINUTES,
	STR_4_MINUTES,
	STR_CONTROLS,
	STR_SOUND,
	STR_GRAPHICS,
	STR_FULLSCREEN_HINT,
	STR_FULLSCREEN,
	STR_ANTIALIASING,
	STR_MSAA_2X,
	STR_MSAA_4X,
	STR_MSAA_8X,
	STR_PREFERRED_DISPLAY,
	STR_DISPLAY,
	STR_ANTIALIASING_CHANGE_WARNING,
	STR_MUSIC,
	STR_SFX,
	STR_VOLUME_000,
	STR_VOLUME_020,
	STR_VOLUME_040,
	STR_VOLUME_060,
	STR_VOLUME_080,
	STR_VOLUME_100,
	STR_CONFIGURE_KEYBOARD,
	STR_CONFIGURE_GAMEPAD,
	STR_CONFIGURE_MOUSE,
	STR_CONFIGURE_KEYBOARD_HELP,
	STR_CONFIGURE_GAMEPAD_HELP,
	STR_CONFIGURE_KEYBOARD_HELP_CANCEL,
	STR_CONFIGURE_GAMEPAD_HELP_CANCEL,
	STR_NO_GAMEPAD_DETECTED,
	STR_LEFT_STICK_ALWAYS_STEERS,
	STR_GAMEPAD_RUMBLE,

	STR_BACK,
	STR_OK,
	STR_CANCEL,
	STR_YES,
	STR_NO,
	STR_ON,
	STR_OFF,

	STR_PRESS,
	STR_CLICK,
	STR_RESET_KEYBINDINGS,
	STR_MOUSE_BUTTON_LEFT,
	STR_MOUSE_BUTTON_MIDDLE,
	STR_MOUSE_BUTTON_RIGHT,
	STR_BUTTON,
	STR_MOUSE_WHEEL_UP,
	STR_MOUSE_WHEEL_DOWN,
	STR_UNBOUND_PLACEHOLDER,
	STR_KEYBINDING_DESCRIPTION_0,
	STR_KEYBINDING_DESCRIPTION_1,
	STR_KEYBINDING_DESCRIPTION_2,
	STR_KEYBINDING_DESCRIPTION_3,
	STR_KEYBINDING_DESCRIPTION_4,
	STR_KEYBINDING_DESCRIPTION_5,
	STR_KEYBINDING_DESCRIPTION_6,
	STR_KEYBINDING_DESCRIPTION_7,
	STR_KEYBINDING_DESCRIPTION_8,

	STR_CLEAR_SAVED_GAME,
	STR_CLEAR_SAVED_GAME_TEXT_1,
	STR_CLEAR_SAVED_GAME_TEXT_2,
	STR_CLEAR_SAVED_GAME_CANCEL,

	STR_CONNECT_CONTROLLERS_PREFIX,
	STR_CONNECT_1_CONTROLLER,
	STR_CONNECT_2_CONTROLLERS,
	STR_CONNECT_3_CONTROLLERS,
	STR_CONNECT_4_CONTROLLERS,
	STR_CONNECT_CONTROLLERS_SUFFIX,
	STR_CONNECT_CONTROLLERS_SUFFIX_KBD,

	STR_SPLITSCREEN_MODE,
	STR_SPLITSCREEN_HORIZ,
	STR_SPLITSCREEN_VERT,

	STR_CREDITS_PROGRAMMING,
	STR_CREDITS_ART,
	STR_CREDITS_MUSIC,
	STR_CREDITS_PORT,
	STR_CREDITS_SPECIAL_THANKS,
	STR_CREDITS_ALL_RIGHTS_RESERVED,

	STR_PHYSICS_SETTINGS_RESET_INFO,
	STR_PHYSICS_EDIT_CAR_STATS,
	STR_PHYSICS_EDIT_CONSTANTS,
	STR_PHYSICS_RESET,
	STR_CAR,
	STR_RESET_THIS_CAR,

	STR_PHYSICS_CONSTANT_STEERING_RESPONSIVENESS,
	STR_PHYSICS_CONSTANT_MAX_TIGHT_TURN,
	STR_PHYSICS_CONSTANT_TURNING_RADIUS,
	STR_PHYSICS_CONSTANT_TIRE_TRACTION,
	STR_PHYSICS_CONSTANT_TIRE_FRICTION,
	STR_PHYSICS_CONSTANT_GRAVITY,
	STR_PHYSICS_CONSTANT_SLOPE_RATIO_ADJUSTER,

	STR_PHYSICS_HELP_STEERING_RESPONSIVENESS,
	STR_PHYSICS_HELP_MAX_TIGHT_TURN,
	STR_PHYSICS_HELP_TURNING_RADIUS,
	STR_PHYSICS_HELP_TIRE_TRACTION,
	STR_PHYSICS_HELP_TIRE_FRICTION,
	STR_PHYSICS_HELP_GRAVITY,
	STR_PHYSICS_HELP_SLOPE_RATIO_ADJUSTER,

	STR_CONTROLLER_BUTTON_A,
	STR_CONTROLLER_BUTTON_B,
	STR_CONTROLLER_BUTTON_X,
	STR_CONTROLLER_BUTTON_Y,
	STR_CONTROLLER_BUTTON_BACK,
	STR_CONTROLLER_BUTTON_GUIDE,
	STR_CONTROLLER_BUTTON_START,
	STR_CONTROLLER_BUTTON_LEFTSTICK,
	STR_CONTROLLER_BUTTON_RIGHTSTICK,
	STR_CONTROLLER_BUTTON_LEFTSHOULDER,
	STR_CONTROLLER_BUTTON_RIGHTSHOULDER,
	STR_CONTROLLER_BUTTON_DPAD_UP,
	STR_CONTROLLER_BUTTON_DPAD_DOWN,
	STR_CONTROLLER_BUTTON_DPAD_LEFT,
	STR_CONTROLLER_BUTTON_DPAD_RIGHT,
	STR_CONTROLLER_AXIS_LEFTSTICK_UP,
	STR_CONTROLLER_AXIS_LEFTSTICK_DOWN,
	STR_CONTROLLER_AXIS_LEFTSTICK_LEFT,
	STR_CONTROLLER_AXIS_LEFTSTICK_RIGHT,
	STR_CONTROLLER_AXIS_RIGHTSTICK_UP,
	STR_CONTROLLER_AXIS_RIGHTSTICK_DOWN,
	STR_CONTROLLER_AXIS_RIGHTSTICK_LEFT,
	STR_CONTROLLER_AXIS_RIGHTSTICK_RIGHT,
	STR_CONTROLLER_AXIS_LEFTTRIGGER,
	STR_CONTROLLER_AXIS_RIGHTTRIGGER,

	STR_MONTH_1,
	STR_MONTH_2,
	STR_MONTH_3,
	STR_MONTH_4,
	STR_MONTH_5,
	STR_MONTH_6,
	STR_MONTH_7,
	STR_MONTH_8,
	STR_MONTH_9,
	STR_MONTH_10,
	STR_MONTH_11,
	STR_MONTH_12,

	NUM_LOCALIZED_STRINGS,
} LocStrID;

void LoadLocalizedStrings(GameLanguageID languageID);
void DisposeLocalizedStrings(void);

const char* Localize(LocStrID stringID);

GameLanguageID GetBestLanguageIDFromSystemLocale(void);
