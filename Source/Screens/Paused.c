/****************************/
/*   	PAUSED.C			*/
/* By Brian Greenstone      */
/* (c)2000 Pangea Software  */
/* (c)2022 Iliyas Jorio     */
/****************************/


/****************************/
/*    EXTERNALS             */
/****************************/

#include "game.h"
#include "menu.h"
#include "network.h"
#include "miscscreens.h"

/****************************/
/*    PROTOTYPES            */
/****************************/

static int ShouldDisplaySplitscreenModeCycler(const MenuItem* mi);
static void OnToggleSplitscreenMode(const MenuItem* mi);


/****************************/
/*    CONSTANTS             */
/****************************/

static const MenuItem gPauseMenuTree[] =
{
	{ .id='paus' },

	{kMIPick, STR_RESUME_GAME, .id='resu', .next='EXIT' },

	{kMISpacer, .customHeight=.3f},

	// 2P split-screen mode chooser
	{
		.type = kMICycler1,
		.text = STR_SPLITSCREEN_MODE,
		.id = 2,	// ShouldDisplaySplitscreenModeCycler looks at this ID to know it's meant for 2P
		.getLayoutFlags = ShouldDisplaySplitscreenModeCycler,
		.callback = OnToggleSplitscreenMode,
		.cycler =
		{
			.valuePtr = &gGamePrefs.splitScreenMode2P,
			.choices =
			{
				{ .text = STR_SPLITSCREEN_HORIZ, .value = SPLITSCREEN_MODE_2P_TALL },
				{ .text = STR_SPLITSCREEN_VERT, .value = SPLITSCREEN_MODE_2P_WIDE },
			},
		},
	},

	// 3P split-screen mode chooser
	{
		.type = kMICycler1,
		.text = STR_SPLITSCREEN_MODE,
		.id = 3,	// ShouldDisplaySplitscreenModeCycler looks at this ID to know it's meant for 3P
		.getLayoutFlags = ShouldDisplaySplitscreenModeCycler,
		.callback = OnToggleSplitscreenMode,
		.cycler =
		{
			.valuePtr = &gGamePrefs.splitScreenMode3P,
			.choices =
			{
				{ .text = STR_SPLITSCREEN_HORIZ, .value = SPLITSCREEN_MODE_3P_TALL },
				{ .text = STR_SPLITSCREEN_VERT, .value = SPLITSCREEN_MODE_3P_WIDE },
			},
		},
	},

	// Race timer
	{
		.type = kMICycler1,
		.text = STR_RACE_TIMER,
		.cycler=
		{
			.valuePtr=&gGamePrefs.raceTimer,
			.choices=
			{
					{STR_RACE_TIMER_HIDDEN, 0},
					{STR_RACE_TIMER_SIMPLE, 1},
					{STR_RACE_TIMER_DETAILED, 2},
			},
		},
	},

	{kMIPick, STR_SETTINGS, .callback=RegisterSettingsMenu, .next='sett' },

	{kMISpacer, .customHeight=.3f},

	{kMIPick, STR_RETIRE_GAME, .id='bail', .next='EXIT' },

//	{kMIPick, STR_QUIT_APPLICATION, .id='quit', .next='EXIT' },

	{ 0 },
};


/*********************/
/*    VARIABLES      */
/*********************/

Boolean gSimulationPaused = false;


/****************** TOGGLE SPLIT-SCREEN MODE ********************/

int ShouldDisplaySplitscreenModeCycler(const MenuItem* mi)
{
	if (gNumSplitScreenPanes == mi->id)
		return 0;
	else
		return kMILayoutFlagHidden | kMILayoutFlagDisabled;
}

void OnToggleSplitscreenMode(const MenuItem* mi)
{
	switch (gNumSplitScreenPanes)
	{
		case 2:
			gActiveSplitScreenMode = gGamePrefs.splitScreenMode2P;
			break;

		case 3:
			gActiveSplitScreenMode = gGamePrefs.splitScreenMode3P;
			break;

		default:
			SDL_Log("%s: what am I supposed to do with %d split-screen panes?", __func__, gNumSplitScreenPanes);
	}

	SetDefaultCameraModeForAllPlayers();
	UpdateCameras(false, true);
}


/********************** DO PAUSED **************************/

static void UpdatePausedMenuCallback(void)
{
	MoveObjects();
	// DoPlayerTerrainUpdate called in DrawPausedScene now

			/* IF DOING NET GAME, LET OTHER PLAYERS KNOW WE'RE STILL GOING SO THEY DONT TIME OUT */

	if (gNetGameInProgress)
	{
		// Burn one net frame
		if (gIsNetworkClient)
		{
			ClientReceive_ControlInfoFromHost();
			// TODO: If net game died, bail here
			ClientSend_ControlInfoToHost();
		}
		else
		{
			HostSend_ControlInfoToClients();
			HostReceive_ControlInfoFromClients();
		}
	}
}

static void DrawPausedScene(void)
{
	// Ensure flags are set (simulate pass 0)
	gCurrentSplitScreenPane = 0;
	DoPlayerTerrainUpdate();
	
	// Draw the scene (will iterate panes if needed)
	OGL_DrawScene(DrawTerrain);
}

void DoPaused(void)
{
	MenuStyle style = kDefaultMenuStyle;
	style.canBackOutOfRootMenu = true;
	style.fadeOutSceneOnExit = false;
	style.darkenPaneOpacity = .6f;
	style.labelColor = (OGLColorRGBA){.7,.7,.7,1};
	style.startButtonExits = true;
	
	//TODO: PushKeys/PopKeys isn't implemented! Save analog?
	PushKeys();										// save key state so things dont get de-synced during net games

	PauseAllChannels(true);

	gSimulationPaused = true;
	gHideInfobar = true;

					/*************/
				/* MAIN LOOP */
				/*************/

	// Reset frame timing to realistic values for UI animation
	// (PlayArea sets gFramesPerSecondFrac to 1/240Hz which is too slow for fade animation)
	gFramesPerSecond = 60.0f;
	gFramesPerSecondFrac = 1.0f / 60.0f;
	CalcFramesPerSecond();
	ReadKeyboard();

	int outcome = StartMenu(gPauseMenuTree, &style, UpdatePausedMenuCallback, DrawPausedScene);

	// In net games, we let PlayArea get us out of gSimulationPaused.
	if (!gNetGameInProgress)
	{
		gSimulationPaused = false;
	}

	PauseAllChannels(false);
	
	PopKeys();										// restore key state


	switch (outcome)
	{
		case	'resu':								// RESUME
		default:
			gHideInfobar = false;
			break;

		case	'bail':								// EXIT
			gGameOver = true;
			break;

		case	'quit':								// QUIT
			CleanQuit();
			break;
	}
}
