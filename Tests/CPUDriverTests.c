#include "game.h"
#include "cpu_driver.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#define CHECK(condition) do { if (!(condition)) { \
	fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); \
	exit(EXIT_FAILURE); } } while (0)

static const uint32_t kBrakes = (uint32_t) 1 << kControlBit_Brakes;
static const uint32_t kForward = (uint32_t) 1 << kControlBit_Forward;
static const uint32_t kBackward = (uint32_t) 1 << kControlBit_Backward;
static const float kTimesteps[] = {50, 60, 72};

// One DoCPUControl_Car step on a straight path: stuck check, skid brake, pedal.
static uint32_t DriveStep(PlayerInfoType *p, const OGLPoint3D *coord, Boolean onGround, float spinRate, float dt)
{
	UpdateCPUStuckCheck(p, coord, dt);
	return CPUPedalControlBits(p, CPUShouldBrakeForSkid(p, onGround, spinRate), true, dt);
}

static void TestBrakeForSkid(void)
{
	PlayerInfoType p = {0};

	CHECK(!CPUShouldBrakeForSkid(&p, true, 0));
	CHECK(CPUShouldBrakeForSkid(&p, true, PI + 0.1f));
	CHECK(CPUShouldBrakeForSkid(&p, true, -PI - 0.1f));
	CHECK(CPUShouldBrakeForSkid(&p, false, PI + 0.1f));				// spinning afloat still brakes
	CHECK(!CPUShouldBrakeForSkid(&p, true, PI - 0.1f));

	p.isPlaning = true;
	CHECK(CPUShouldBrakeForSkid(&p, true, 0));
	CHECK(!CPUShouldBrakeForSkid(&p, false, 0));
	p.greasedTiresTimer = 0.5f;
	CHECK(CPUShouldBrakeForSkid(&p, true, 0));
	CHECK(!CPUShouldBrakeForSkid(&p, false, 0));
	p.isPlaning = false;
	CHECK(CPUShouldBrakeForSkid(&p, true, 0));
	CHECK(!CPUShouldBrakeForSkid(&p, false, 0));
}

static void TestPedal(void)
{
	PlayerInfoType p = {0};

	p.reverseTimer = 0.5f;
	CHECK(CPUPedalControlBits(&p, true, true, 0.2f) == kBrakes);		// brake beats gas and reverse
	CHECK(p.reverseTimer == 0.5f);
	CHECK(CPUPedalControlBits(&p, false, false, 0.2f) == 0);
	CHECK(p.reverseTimer == 0.5f);
	CHECK(CPUPedalControlBits(&p, false, true, 0.2f) == kBackward);
	CHECK(fabsf(p.reverseTimer - 0.3f) < 1e-6f);
	CHECK(CPUPedalControlBits(&p, false, true, 0.4f) == kBackward);
	CHECK(p.reverseTimer == 0);
	CHECK(CPUPedalControlBits(&p, false, true, 0.4f) == kForward);
}

// Returns the simulation time of each change between backing up and driving forward.
static int RunStuckCheck(float fps, float speed, Boolean onWater, float seconds, float *toggles, int maxToggles)
{
	PlayerInfoType p = {0};
	OGLPoint3D coord = {1000, 0, 1000};
	const float dt = 1.0f / fps;
	Boolean reversing = false;
	int n = 0;

	p.onWater = onWater;
	for (int frame = 1; frame <= (int) (seconds * fps); frame++)
	{
		uint32_t bits = DriveStep(&p, &coord, !onWater, 0, dt);
		CHECK(bits == kForward || bits == kBackward);
		if ((bits == kBackward) != reversing)
		{
			reversing = !reversing;
			if (n < maxToggles)
				toggles[n] = frame * dt;
			n++;
		}
		coord.x += speed * dt;
	}
	return n;
}

static void TestStuckCheck(void)
{
	for (int i = 0; i < 3; i++)
	{
		const float fps = kTimesteps[i];
		float toggles[8];

			/* A stationary car backs up for a second, drives for a second, and so on */

		int n = RunStuckCheck(fps, 0, false, 6.5f, toggles, 8);
		CHECK(n == 6);
		for (int k = 0; k < n; k++)
			CHECK(fabsf(toggles[k] - (1.0f + k)) <= 1.0f / fps + 0.001f);	// timers follow sim time, not frames

			/* Progress thresholds: 80 units per check on land, 40 afloat */

		CHECK(RunStuckCheck(fps, 100, false, 6.5f, toggles, 8) == 0);
		CHECK(RunStuckCheck(fps, 60, false, 6.5f, toggles, 8) > 0);
		CHECK(RunStuckCheck(fps, 60, true, 6.5f, toggles, 8) == 0);
		CHECK(RunStuckCheck(fps, 20, true, 6.5f, toggles, 8) > 0);
	}
}

// Egypt regression: a hard car-car hit sets greased tires and planing, and the car
// slides into the Nile. Planing only expires on the ground, so afloat it never did;
// the CPU braked for it every frame and sat in the river for the rest of the race.
static void TestFloatingCarWithStalePlaningDrives(void)
{
	for (int i = 0; i < 3; i++)
	{
		const float dt = 1.0f / kTimesteps[i];
		PlayerInfoType p = {0};
		OGLPoint3D coord = {5000, 200, 5000};

		p.onWater = true;
		p.isPlaning = true;
		p.greasedTiresTimer = 0.5f;
		for (int frame = 0; frame < (int) (10.0f * kTimesteps[i]); frame++)
		{
			uint32_t bits = DriveStep(&p, &coord, false, 0, dt);
			CHECK(!(bits & kBrakes));
			if (bits & kForward)
				coord.x += 300.0f * dt;
			if (bits & kBackward)
				coord.x -= 300.0f * dt;
		}
		CHECK(coord.x - 5000 > 2500);
		CHECK(p.isPlaning && p.greasedTiresTimer == 0.5f);			// state is untouched

			/* Same state on land: planing still means brake */

		CHECK(DriveStep(&p, &coord, true, 0, dt) == kBrakes);
	}
}

/************************** CPU RESCUE ****************************/

// Seconds of no forward progress until the rescue fires, at this timestep.
static float SecondsUntilRescue(PlayerInfoType *p, int progress, float fps, float limit)
{
	const float dt = 1.0f / fps;
	for (float t = dt; t <= limit; t += dt)
		if (UpdateCPURescueTimer(p, progress, 5000, true, dt))
			return t;
	return -1;
}

static void TestRescueTimer(void)
{
	for (int f = 0; f < 3; f++)
	{
		const float fps = kTimesteps[f];
		PlayerInfoType p = {0};
		p.rescueProgress = -1;

		// Twenty seconds without a new best progress, then again twenty seconds later.
		CHECK(!UpdateCPURescueTimer(&p, 0, 5000, true, 1.0f / fps));				// crossed the finish line
		const float first = SecondsUntilRescue(&p, 0, fps, 30);
		CHECK(first >= CPU_RESCUE_TIME && first < CPU_RESCUE_TIME + 2.0f / fps);
		const float second = SecondsUntilRescue(&p, 0, fps, 30);
		CHECK(second >= CPU_RESCUE_TIME && second < CPU_RESCUE_TIME + 2.0f / fps);

		// Each new checkpoint restarts the count; going back or losing a lap doesn't.
		p.rescueTimer = 0;
		for (int i = 0; i < (int) (15 * fps); i++)
			CHECK(!UpdateCPURescueTimer(&p, 0, 5000, true, 1.0f / fps));
		CHECK(!UpdateCPURescueTimer(&p, 1, 5000, true, 1.0f / fps));
		CHECK(p.rescueTimer == 0 && p.rescueProgress == 1);
		const float backward = SecondsUntilRescue(&p, 0, fps, 30);
		CHECK(backward >= CPU_RESCUE_TIME && backward < CPU_RESCUE_TIME + 2.0f / fps);

		// A car that keeps getting closer to the next checkpoint, however slowly, is left
		// alone; one that only goes back and forth (on a fence) is rescued once its first
		// swing toward the checkpoint (which counts as progress) is 20 s old.
		PlayerInfoType slow = {0};
		slow.rescueProgress = -1;
		CHECK(!UpdateCPURescueTimer(&slow, 0, 20000, true, 1.0f / fps));
		for (int i = 0; i < (int) (90 * fps); i++)								// 150 units a second
			CHECK(!UpdateCPURescueTimer(&slow, 0, 20000 - 150.0f * (float) i / fps, true, 1.0f / fps));
		PlayerInfoType rocking = {0};
		rocking.rescueProgress = -1;
		CHECK(!UpdateCPURescueTimer(&rocking, 0, 8000, true, 1.0f / fps));
		Boolean rescued = false;
		for (int i = 0; i < (int) (30 * fps) && !rescued; i++)					// swings 600 units either way
			rescued = UpdateCPURescueTimer(&rocking, 0, 8000 + 600.0f * sinf((float) i / fps), true, 1.0f / fps);
		CHECK(rescued);

		// Starting lights and finished races never count.
		for (int i = 0; i < (int) (60 * fps); i++)
			CHECK(!UpdateCPURescueTimer(&p, 1, 5000, false, 1.0f / fps));
		CHECK(p.rescueTimer == 0);
	}

	// Race progress: laps count every checkpoint, and the start (lap -1, checkpoint N-1) is -1.
	CHECK(CPURaceProgress(-1, 20, 21) == -1);
	CHECK(CPURaceProgress(0, 0, 21) == 0 && CPURaceProgress(1, 3, 21) == 24);
	CHECK(CPURaceProgress(2, 0, 21) > CPURaceProgress(1, 20, 21));
}

static void TestRescueSpot(void)
{
	PlayerInfoType p = {0};
	CPURescueSpot spot;

	// Just past where the car last crossed a checkpoint going forward, facing the way it
	// was driving then (cars face (-sin rotY, -cos rotY)).
	RecordCPURescueCrossing(&p, 1000, -2000, 0, -30);							// driving toward -z
	CHECK(FindCPURescueSpot(&p, NULL, 0, &spot));
	CHECK(spot.x == 1000 && spot.z == -2000 - CPU_RESCUE_AHEAD);
	CHECK(fabsf(-sinf(spot.rotY)) < 0.001f && fabsf(-cosf(spot.rotY) - (-1)) < 0.001f);

	// A diagonal crossing keeps its direction, normalized.
	RecordCPURescueCrossing(&p, 0, 0, 3, 4);
	CHECK(FindCPURescueSpot(&p, NULL, 0, &spot));
	CHECK(fabsf(spot.x - 0.6f * CPU_RESCUE_AHEAD) < 0.01f && fabsf(spot.z - 0.8f * CPU_RESCUE_AHEAD) < 0.01f);
	CHECK(fabsf(-sinf(spot.rotY) - 0.6f) < 0.001f && fabsf(-cosf(spot.rotY) - 0.8f) < 0.001f);

	// A car in the way: further along, a step at a time.
	RecordCPURescueCrossing(&p, 1000, -2000, 0, -1);
	OGLPoint3D others[3] = { {1000, 0, -2000 - CPU_RESCUE_AHEAD} };
	CHECK(FindCPURescueSpot(&p, others, 1, &spot));
	CHECK(spot.x == 1000 && spot.z == -2000 - CPU_RESCUE_AHEAD - CPU_RESCUE_STEP);

	// Every place taken: no spot, and the caller's spot is left alone (never overlap a car).
	others[1] = (OGLPoint3D) {1000, 0, -2000 - CPU_RESCUE_AHEAD - CPU_RESCUE_STEP};
	others[2] = (OGLPoint3D) {1000, 0, -2000 - CPU_RESCUE_AHEAD - 2 * CPU_RESCUE_STEP};
	const CPURescueSpot untouched = {1, 2, 3};
	spot = untouched;
	CHECK(!FindCPURescueSpot(&p, others, 3, &spot));
	CHECK(spot.x == 1 && spot.z == 2 && spot.rotY == 3);

	// A zero-length move keeps the previous direction; the point still moves.
	RecordCPURescueCrossing(&p, 50, 60, 0, 0);
	CHECK(p.rescueX == 50 && p.rescueZ == 60 && p.rescueDirX == 0 && p.rescueDirZ == -1);
}

int main(void)
{
	TestBrakeForSkid();
	TestPedal();
	TestStuckCheck();
	TestFloatingCarWithStalePlaningDrives();
	TestRescueTimer();
	TestRescueSpot();
	puts("cpu driver tests passed");
	return EXIT_SUCCESS;
}
