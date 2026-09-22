#include "game.h"
#include "cpu_driver.h"
#include <stdio.h>
#include <stdlib.h>

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

int main(void)
{
	TestBrakeForSkid();
	TestPedal();
	TestStuckCheck();
	TestFloatingCarWithStalePlaningDrives();
	puts("cpu driver tests passed");
	return EXIT_SUCCESS;
}
