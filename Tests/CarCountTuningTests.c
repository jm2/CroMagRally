#include "car_count_tuning.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) do { if (!(condition)) { \
	fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); \
	exit(EXIT_FAILURE); } } while (0)

#define MAX_TEST_CARS	32

static int SameBits(float a, float b)
{
	return memcmp(&a, &b, sizeof(float)) == 0;
}

// The per-place steps DoCarMotion and DoSubmarineMotion scale.
static const float kPlaceSteps[] = {0.3f, 170.0f, 200.0f, 100.0f};

static void TestSixCarsOrFewerKeepTheOriginalStep(void)
{
	for (int n = -1; n <= TUNED_NUM_CARS; n++)
		CHECK(SameBits(GetCatchUpPlaceScale(n), 1.0f));

		// Call sites multiply the per-place step by the scale. With exactly 1 the step,
		// and every product and sum built from it, keeps its bits.

	volatile float scale = GetCatchUpPlaceScale(TUNED_NUM_CARS);
	for (size_t k = 0; k < sizeof(kPlaceSteps) / sizeof(kPlaceSteps[0]); k++)
	{
		const float step = kPlaceSteps[k];
		for (int place = 0; place < TUNED_NUM_CARS; place++)
		{
			CHECK(SameBits((float) place * (step * scale), (float) place * step));
			CHECK(SameBits(1.0f + (float) place * (step * scale), 1.0f + (float) place * step));
			CHECK(SameBits(2400.0f + (float) place * (step * scale), 2400.0f + (float) place * step));
		}
	}
}

static void TestLargerFieldsShareTheSixCarRange(void)
{
	CHECK(SameBits(GetCatchUpPlaceScale(12), 5.0f / 11.0f));
	CHECK(SameBits(GetCatchUpPlaceScale(7), 5.0f / 6.0f));

	float previous = GetCatchUpPlaceScale(TUNED_NUM_CARS);
	for (int n = TUNED_NUM_CARS + 1; n <= MAX_TEST_CARS; n++)
	{
		const float scale = GetCatchUpPlaceScale(n);
		CHECK(scale > 0.0f && scale < previous);
		previous = scale;

			// last place gets what 6th place got in a six-car race

		for (size_t k = 0; k < sizeof(kPlaceSteps) / sizeof(kPlaceSteps[0]); k++)
		{
			const float step = kPlaceSteps[k];
			const float lastPlace = (float) (n - 1) * (step * scale);
			CHECK(fabsf(lastPlace - (float) (TUNED_NUM_CARS - 1) * step) <= 1e-5f * step * TUNED_NUM_CARS);
		}
	}

	const float hardTraction12 = 1.0f + 11.0f * (0.3f * GetCatchUpPlaceScale(12));
	CHECK(fabsf(hardTraction12 - 2.5f) < 1e-5f);				// the most a 12th-place CPU grips, as 6th of 6
}

static void TestPOWRespawn(void)
{
	for (int n = -1; n <= TUNED_NUM_CARS; n++)
		CHECK(SameBits(GetPOWRespawnScale(n), 1.0f));

	volatile float delay = 5.0f;										// Triggers.c POW_RESPAWN_DELAY
	CHECK(SameBits(delay * GetPOWRespawnScale(TUNED_NUM_CARS), 5.0f));
	CHECK(SameBits(GetPOWRespawnScale(12), 0.5f));
	CHECK(SameBits(delay * GetPOWRespawnScale(12), 2.5f));

		// Each car sees as many respawns per second as one of six cars did.

	float previous = GetPOWRespawnScale(TUNED_NUM_CARS);
	for (int n = TUNED_NUM_CARS + 1; n <= MAX_TEST_CARS; n++)
	{
		const float scale = GetPOWRespawnScale(n);
		CHECK(scale > 0.0f && scale < previous);
		CHECK(fabsf((float) n * scale - (float) TUNED_NUM_CARS) < 1e-5f);
		previous = scale;
	}
}

static void TestEliminationTagTime(void)
{
	// Six players or fewer keep the whole allowance, to the bit.
	for (int n = 0; n <= TUNED_NUM_CARS; n++)
		CHECK(SameBits(GetEliminationTagTimeScale(n), 1.0f));

	// Above six, (players-1) allowances add up to what five did with six players.
	for (int n = TUNED_NUM_CARS + 1; n <= 16; n++)
	{
		const float total = (float)(n - 1) * GetEliminationTagTimeScale(n);
		CHECK(total > (float)(TUNED_NUM_CARS - 1) - 0.001f && total < (float)(TUNED_NUM_CARS - 1) + 0.001f);
	}
	CHECK(SameBits(GetEliminationTagTimeScale(12), 5.0f / 11.0f));
}

int main(void)
{
	TestEliminationTagTime();
	TestSixCarsOrFewerKeepTheOriginalStep();
	TestLargerFieldsShareTheSixCarRange();
	TestPOWRespawn();
	puts("car count tuning tests passed");
	return EXIT_SUCCESS;
}
