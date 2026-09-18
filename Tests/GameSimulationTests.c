#include "game.h"
#include "network.h"
#include <stdio.h>
#include <stdlib.h>

#define CHECK(condition) do { if (!(condition)) { \
	fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); \
	exit(EXIT_FAILURE); } } while (0)

Boolean gGameOver, gTrackCompleted, gSimulationPaused;
float gTrackCompletedCoolDownTimer, gFramesPerSecondFrac;
uint32_t gSimulationFrame;
static Boolean paused, leaveEvent, finishDuringStep;
static int moves, pausedMoves, terrainUpdates, events, banners;

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
}

static int RunSchedule(const int* renders, int numRenders, Boolean inMenu)
{
	// The same authoritative stream contains variable timesteps and a pause.
	const float dt[] = {0.125f, 0.25f, 0.5f, 0.25f, 0.125f, 0.25f};
	const Boolean pause[] = {false, false, true, true, false, false};
	Reset();
	finishDuringStep = true;
	int packet = 0;
	for (int render = 0; render < 30; render++)
	{
		float before = gTrackCompletedCoolDownTimer;
		int steps = renders[render % numRenders];
		for (int k = 0; k < steps; k++)
		{
			CHECK(packet < 6);
			gFramesPerSecondFrac = dt[packet];
			paused = pause[packet++];
			if (StepGameSimulation(!inMenu))
			{
				CHECK(gTrackCompletedCoolDownTimer == 0);
				CHECK(moves == 4 && pausedMoves == 2 && terrainUpdates == 6);
				CHECK(gSimulationFrame == 4 && events == 6);
				CHECK(banners == (inMenu ? 0 : 2));
				// No later packet or repeated call may advance the completed race.
				CHECK(StepGameSimulation(!inMenu));
				CHECK(moves == 4 && events == 6);
				return packet;
			}
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
	CHECK(RunSchedule(holds, 5, false) == 6);
	CHECK(RunSchedule(holds, 5, true) == 6);

	Reset();
	gFramesPerSecondFrac = 0.5f;
	CHECK(!StepGameSimulation(true));
	CHECK(!gTrackCompleted && gTrackCompletedCoolDownTimer == 0);
	leaveEvent = true;
	CHECK(StepGameSimulation(true));
	CHECK(gGameOver && moves == 1 && terrainUpdates == 1);
	puts("Simulation completion tests passed");
	return 0;
}
