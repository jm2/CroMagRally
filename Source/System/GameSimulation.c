#include "game.h"
#include "network.h"
#include "miscscreens.h"

Boolean IsGameSimulationComplete(void)
{
	return gGameOver || (gTrackCompleted && gTrackCompletedCoolDownTimer <= 0.0f);
}

// CMR8: one authoritative step per applied host packet, shared by gameplay and
// the client pause menu while its pause request is still travelling to the host.
// Return true at the completion frame so catch-up never steps past that frame.
Boolean StepGameSimulation(Boolean showPauseScreen)
{
	if (IsGameSimulationComplete())
		return true;

	// Apply events after the seed exchange and before any simulation RNG draws.
	ApplyPendingFrameEvents();
	if (gGameOver)
		return true;

	gSimulationPaused = IsNetGamePaused();
	if (gSimulationPaused)
	{
		if (showPauseScreen)
			SetupNetPauseScreen();
		MoveObjects();
	}
	else
	{
		if (showPauseScreen)
			RemoveNetPauseScreen();
		MoveEverything();
		UpdateGameModeSpecifics();
	}
	DoPlayerTerrainUpdate();
	if (!gSimulationPaused)
	{
		gSimulationFrame++;

		// Charge the same dt as movement, including the frame that completes the
		// race. Hold renders and paused host packets must not consume cooldown.
		if (gTrackCompleted)
			gTrackCompletedCoolDownTimer = GAME_MAX(0.0f,
				gTrackCompletedCoolDownTimer - gFramesPerSecondFrac);
	}
	return IsGameSimulationComplete();
}
