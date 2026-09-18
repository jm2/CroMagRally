#include "game.h"
#include "network.h"
#include <stdio.h>
#include <stdlib.h>

#define CHECK(condition) do { if (!(condition)) { \
	fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); \
	exit(EXIT_FAILURE); } } while (0)

Boolean gGameOver, gTrackCompleted, gSimulationPaused;
Boolean gIsNetworkClient, gNetGameInProgress;
int gClientCatchUpMax;
float gTrackCompletedCoolDownTimer, gFramesPerSecondFrac;
uint32_t gSimulationFrame;
static Boolean paused, leaveEvent, finishDuringStep;
static int moves, pausedMoves, terrainUpdates, events, banners;
static int retained, packet, available, consumed;
static const float dt[] = {0.125f, 0.25f, 0.5f, 0.25f, 0.125f, 0.25f, 1, 1};
static const Boolean pause[] = {false, false, true, true, false, false, false, false};

HostConsumeResult Client_ConsumeHostPacketFromRing(void)
{
	consumed++;
	if (packet == available)
		return kHostConsume_Empty;
	CHECK(packet < 8);
	gFramesPerSecondFrac = dt[packet];
	paused = pause[packet++];
	return kHostConsume_Applied;
}
void KeepTerrainAliveForRender(void) { retained++; }

void ApplyPendingFrameEvents(void) { events++; if (leaveEvent) gGameOver = true; }
Boolean IsNetGamePaused(void) { return paused; }
void SetupNetPauseScreen(void) { banners++; }
void RemoveNetPauseScreen(void) {}
void MoveObjects(void) { CHECK(gSimulationPaused); pausedMoves++; }
void MoveEverything(void) { CHECK(!gSimulationPaused); moves++; }
void UpdateGameModeSpecifics(void)
{
	if (finishDuringStep && !gTrackCompleted)
	{
		gTrackCompleted = true;
		gTrackCompletedCoolDownTimer = 0.75f;
	}
}
void DoPlayerTerrainUpdate(void) { terrainUpdates++; }

static void Reset(void)
{
	gGameOver = gTrackCompleted = gSimulationPaused = false;
	paused = leaveEvent = finishDuringStep = false;
	gSimulationFrame = 0;
	gTrackCompletedCoolDownTimer = 0;
	moves = pausedMoves = terrainUpdates = events = banners = 0;
	retained = packet = available = consumed = 0;
	gIsNetworkClient = gNetGameInProgress = true;
	gClientCatchUpMax = 3;
}

static int RunSchedule(const int* renders, int numRenders, Boolean inMenu)
{
	// The same authoritative stream contains variable timesteps and a pause.
	Reset();
	finishDuringStep = true;
	for (int render = 0; render < 30; render++)
	{
		float before = gTrackCompletedCoolDownTimer;
		int steps = renders[render % numRenders];
		available = GAME_MIN(8, available + steps);
		// Queue surplus packets with the completion packet to exercise the real
		// catch-up consumer shared by gameplay and the pause menu.
		if (available >= 6)
			available = 8;
		AdvanceClientSimulation(!inMenu);
		if (IsGameSimulationComplete())
		{
				CHECK(gTrackCompletedCoolDownTimer == 0);
				CHECK(moves == 4 && pausedMoves == 2 && terrainUpdates == 6);
				CHECK(gSimulationFrame == 4 && events == 6);
				CHECK(banners == (inMenu ? 0 : 2));
				// No later packet or repeated call may advance the completed race.
				CHECK(StepGameSimulation(!inMenu));
				int beforeConsumed = consumed;
				CHECK(!AdvanceClientSimulation(!inMenu));
				CHECK(consumed == beforeConsumed && packet == 6 && available == 8);
				CHECK(moves == 4 && events == 6);
				return packet;
		}
		if (steps == 0)
			CHECK(gTrackCompletedCoolDownTimer == before);
	}
	CHECK(false);
	return -1;
}

int main(void)
{
	const int one[] = {1}, catchUp[] = {3}, holds[] = {0, 0, 3, 0, 1};
	CHECK(RunSchedule(one, 1, false) == 6);
	CHECK(RunSchedule(catchUp, 1, false) == 6);
	CHECK(RunSchedule(catchUp, 1, true) == 6);
	CHECK(RunSchedule(holds, 5, false) == 6);
	CHECK(RunSchedule(holds, 5, true) == 6);

	Reset();
	gFramesPerSecondFrac = 0.5f;
	CHECK(!StepGameSimulation(true));
	CHECK(!gTrackCompleted && gTrackCompletedCoolDownTimer == 0);
	leaveEvent = true;
	CHECK(StepGameSimulation(true));
	CHECK(gGameOver && moves == 1 && terrainUpdates == 1);
	Reset();
	leaveEvent = true;
	available = 3;
	CHECK(AdvanceClientSimulation(false));
	CHECK(gGameOver && moves == 0 && terrainUpdates == 0 && retained == 1);
	CHECK(packet == 1);  // departure must also leave later packets queued
	puts("Simulation completion tests passed");
	return 0;
}
