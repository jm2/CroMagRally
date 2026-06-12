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
			/* CMR7 CLIENT-INITIATED PAUSE: STAY IN LOCKSTEP UNTIL THE HOST CONFIRMS THE PAUSE */
	//
	// The host never waits (free-running Net_Pump + Host_ConsumeClientInputs). If a pausing
	// client froze its sim immediately, the host would keep advancing MoveEverything for the
	// whole uplink round-trip while the client did not — so the per-frame seed check
	// (MyRandomLong() vs the host's broadcast seed) would diverge within ~2 packets and fire a
	// kNetSequence_SeedDesync FATAL. Instead, mirror the Main.c game loop here: consume the
	// host packet, then advance the sim normally while the host is still running, and only
	// switch to the frozen (MoveObjects-only) branch once the host's broadcast itself reports
	// the net pause. We re-assert our own pauseState each iteration so the host latches the
	// pause (ClientReceive overwrites our local copy with the host's lagging view).

	if (gNetGameInProgress && gIsNetworkClient)
	{
		ClientReceive_ControlInfoFromHost();

		// The receive can tear the net game down (e.g. the host left -> EndNetworkGame
		// clears gIsNetworkClient). Don't send into a dead session: ClientSend asserts
		// gIsNetworkClient and would crash both this client's process.
		if (!gIsNetworkClient || !gNetGameInProgress)
			return;

		// Evaluate BEFORE re-asserting our own intent below, so this reflects ONLY the host's
		// (just-received) broadcast — i.e. whether the host has actually begun the net pause.
		Boolean hostConfirmedPause = IsNetGamePaused();

		if (hostConfirmedPause)
		{
			gSimulationPaused = true;
			MoveObjects();								// frozen step — host is running MoveObjects too
			DoPlayerTerrainUpdate();
		}
		else
		{
			gSimulationPaused = false;
			MoveEverything();							// stay in lockstep: same RNG draw count as the host
			UpdateGameModeSpecifics();
			DoPlayerTerrainUpdate();
			gSimulationFrame++;
		}

		// Re-assert our local pause intent (the menu is open) so the host latches & broadcasts
		// the net pause; ClientReceive just clobbered our copy with the host's lagging view.
		gPlayerInfo[gMyNetworkPlayerNum].net.pauseState = 1;
		ClientSend_ControlInfoToHost();
		return;
	}

	MoveObjects();
	DoPlayerTerrainUpdate();							// need to call this to keep supertiles active


			/* IF DOING NET GAME, LET OTHER PLAYERS KNOW WE'RE STILL GOING SO THEY DONT TIME OUT */

	if (gNetGameInProgress && gIsNetworkHost)
	{
		// CMR7: the host never blocks — drain client inputs (non-blocking), resolve them
		// (substitute/coalesce), then broadcast, keeping the full-rate lockstep alive while
		// the pause menu is open.
		Net_Pump();
		Host_ConsumeClientInputs();
		// A send failure can call NetGameFatalError -> EndNetworkGame (clears gIsNetworkHost).
		if (gIsNetworkHost && gNetGameInProgress)
			HostSend_ControlInfoToClients();
	}
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

	CalcFramesPerSecond();
	ReadKeyboard();

	int outcome = StartMenu(gPauseMenuTree, &style, UpdatePausedMenuCallback, DrawTerrain);

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
