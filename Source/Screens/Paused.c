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
	// kNetSequence_SeedDesync FATAL. Instead, mirror the Main.c free-running client path here:
	// Net_Pump + bounded ring catch-up (consume up to K_max host packets, one sim step each) +
	// the wall-clock uplink sampler — so a downlink hiccup never silences the uplink (no more
	// blocking ClientReceive shim / DATA_TIMEOUT fatal). We stay at full step (lockstep with the
	// still-running host) until the host's own broadcast reports the net pause, then freeze.

	if (gNetGameInProgress && gIsNetworkClient)
	{
		Net_Pump();										// drain host packets into the ring + flush send rings (non-blocking)
		NetCheck_ConnectionTimeouts();					// CMR7 Stage 4: host-link badge/drop policy (errors out the menu below if the host is gone)

		int guard = 0;
		for (int k = 0; k < gClientCatchUpMax; )		// bounded catch-up (K_max), mirroring the main loop
		{
			HostConsumeResult r = Client_ConsumeHostPacketFromRing();
			if (r == kHostConsume_Applied)
			{
				// The handler just overwrote every player's pauseState with the host's broadcast
				// view, so IsNetGamePaused() here reflects ONLY whether the host has begun the net
				// pause. Apply frame-aligned events AFTER the seed check (in the handler) and BEFORE
				// MoveEverything, exactly as StepGameSimulation does. NB: we deliberately do NOT call
				// SetupNetPauseScreen here — the pause menu is already on screen.
				ApplyPendingFrameEvents();

				if (IsNetGamePaused())
				{
					gSimulationPaused = true;
					MoveObjects();						// frozen step — the host is running MoveObjects too
					DoPlayerTerrainUpdate();
				}
				else
				{
					gSimulationPaused = false;
					MoveEverything();					// stay in lockstep: same RNG draw count as the host
					UpdateGameModeSpecifics();
					DoPlayerTerrainUpdate();
					gSimulationFrame++;
				}
				k++;
			}
			else if (r == kHostConsume_Dup)
			{
				if (++guard > 2*gClientCatchUpMax)		// defense vs a pathological dup storm
					break;
			}
			else
			{
				break;									// ring empty -> hold (no sim step this menu frame)
			}

			// Consuming can tear the net game down (host left -> EndNetworkGame clears gIsNetworkClient).
			if (!gIsNetworkClient || !gNetGameInProgress)
				break;
		}

		// Re-assert our pause intent (the menu is open) and keep the wall-clock uplink alive even
		// across a downlink stall. Seeding schedulePause=true forces pauseState=1 each emitted packet
		// so the host latches & broadcasts the net pause.
		if (gIsNetworkClient && gNetGameInProgress)
		{
			Boolean schedulePause = true;
			SampleAndSendLocalInput(&schedulePause);
			Net_Pump();								// flush the just-enqueued input packets
		}

		// CMR7 Stage 4: a dead net game (host gone / everybody left) must break the menu so PlayArea
		// can tear it down, instead of spinning the pause loop forever.
		if (gNetSequenceState >= kNetSequence_Error || gGameOver)
			KillMenu('bail');
		return;
	}


			/* IF DOING NET GAME, LET OTHER PLAYERS KNOW WE'RE STILL GOING SO THEY DONT TIME OUT */
	//
	// CMR7 Stage 4: the net block (Net_Pump + Host_ConsumeClientInputs + HostSend, which draws the
	// per-frame seed via MyRandomLong) runs BEFORE MoveObjects, matching the main loop — the seed is
	// the first synced draw on every peer in every pause path. Then frame-aligned events apply (after
	// the seed, before MoveObjects), then the frozen step.

	if (gNetGameInProgress && gIsNetworkHost)
	{
		// The host never blocks — drain client inputs (non-blocking), resolve them
		// (substitute/coalesce), then broadcast, keeping the full-rate lockstep alive while
		// the pause menu is open.
		Net_Pump();
		NetCheck_ConnectionTimeouts();					// CMR7 Stage 4: per-client badge/drop policy (schedules frame-aligned become-bot)
		Host_ConsumeClientInputs();
		// A send failure can call NetGameFatalError -> EndNetworkGame (clears gIsNetworkHost).
		if (gIsNetworkHost && gNetGameInProgress)
			HostSend_ControlInfoToClients();
	}

	ApplyPendingFrameEvents();							// become-bot at the right frame (no-op outside a net game)

	MoveObjects();
	DoPlayerTerrainUpdate();							// need to call this to keep supertiles active

	if (gNetGameInProgress && gIsNetworkHost
		&& (gNetSequenceState >= kNetSequence_Error || gGameOver))
		KillMenu('bail');							// CMR7 Stage 4: break the menu so PlayArea tears down a dead net game
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
