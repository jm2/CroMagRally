#include "game.h"
#include "vehicle_picker.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) do { if (!(condition)) { \
	fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); \
	exit(EXIT_FAILURE); } } while (0)

#define MAX_TEST_PLAYERS	16		// beyond MAX_PLAYERS, to cover larger grids
#define MAX_TEST_HUMANS		4
#define NUM_TEST_AGES		(NUM_AGES + 1)	// GetNumAgesCompleted returns 0...NUM_AGES

_Static_assert(MAX_TEST_PLAYERS >= MAX_PLAYERS, "cover every supported slot count");

void DoFatalAlert(const char* format, ...) { (void)format; abort(); }

// Deterministic stand-in for the synced RNG. It logs every call so both pickers can
// be shown to make the same draws in the same order.
typedef struct { uint16_t min, max; int cpuIndex; } RandomCall;
static uint32_t rngState;
static int numRandomCalls;
static RandomCall randomCalls[MAX_TEST_PLAYERS];
static int nextCPUIndex = -1;		// cpuIndex the next call is made for; -1 from the old picker

uint16_t RandomRange(unsigned short min, unsigned short max)
{
	CHECK(min <= max);
	CHECK(numRandomCalls < MAX_TEST_PLAYERS);
	randomCalls[numRandomCalls++] = (RandomCall){min, max, nextCPUIndex};
	rngState = rngState * 1664525u + 1013904223u;
	return (uint16_t)(min + (rngState >> 8) % (max - min + 1u));
}

static int randomContextTag;
static uint16_t TestRandom(void* context, int cpuIndex, uint16_t min, uint16_t max)
{
	CHECK(context == &randomContextTag);
	nextCPUIndex = cpuIndex;
	return RandomRange(min, max);
}

typedef struct
{
	Boolean	isComputer;
	short	vehicleType;
} TestPlayer;

static TestPlayer testPlayers[MAX_TEST_PLAYERS];
static int testNumPlayers, testDifficulty, testAgesCompleted;
int GetNumAgesCompleted(void) { return testAgesCompleted; }

// The CPU vehicle choice from InitPlayersAtStartOfLevel at f72596a, verbatim apart from
// reading test state. taken[] gets unused slack below index 0: with more CPUs than free
// cars the old loop walks off the front of the table, and the slack lets the reference
// report the negative types it produced there instead of reading out of bounds.
#define gPlayerInfo			testPlayers
#define gNumTotalPlayers	testNumPlayers
#define gDifficulty			testDifficulty
#define gNetGameInProgress	false
static void OldPickCPUVehicles(void)
{
int		i,type;
Boolean	takenStorage[MAX_TEST_PLAYERS + NUM_LAND_CAR_TYPES] = {0};
Boolean	*taken = takenStorage + MAX_TEST_PLAYERS;


		/* FIRST MARK WHICH CAR TYPES THE HUMANS HAVE */

	for (i = 0; i < NUM_LAND_CAR_TYPES; i++)						// first mark all unused
		taken[i] = false;

	for (i = 0; i < gNumTotalPlayers; i++)
	{
		if (!gPlayerInfo[i].isComputer)								// check for human player
		{
			GAME_ASSERT(gPlayerInfo[i].vehicleType >= 0);
			GAME_ASSERT(gPlayerInfo[i].vehicleType < NUM_LAND_CAR_TYPES);
			taken[gPlayerInfo[i].vehicleType] = true;				// mark this used
		}
	}


	i = GetNumAgesCompleted();
	if (i > 2)														// dont get extra cars after winning, so pin @ 2
		i = 2;
	type = 6 + (i * 2)-1;											// start @ end of usable cars so it will pick best cars


			/* SET SOME GLOBALS */

	for (i = 0; i < gNumTotalPlayers; i++)
	{
		// Network replacements retain the shared selection (or its default).
		// Local unlock progress must not change their vehicle or consume synced RNG.
		if (gPlayerInfo[i].isComputer && !gNetGameInProgress)		// set local CPU vehicle type
		{
			if (gDifficulty == DIFFICULTY_HARD)					// in hard mode, the CPU can have duplicate cars
				gPlayerInfo[i].vehicleType = RandomRange(0, type);
			else														// in other difficulty modes, only choose unique cars
			{
				while(taken[type])										// skip over vehicles already used by Humans
					type--;

				gPlayerInfo[i].vehicleType = type--;
			}
		}
	}
}
#undef gPlayerInfo
#undef gNumTotalPlayers
#undef gDifficulty
#undef gNetGameInProgress

// The same selection through the extracted picker, wired as InitPlayersAtStartOfLevel
// now wires it.
static void NewPickCPUVehicles(void)
{
	CPUVehiclePickRules rules = { .randomRange = TestRandom, .randomContext = &randomContextTag };
	for (int i = 0; i < testNumPlayers; i++)
		if (!testPlayers[i].isComputer)
			rules.humanCarMask |= 1u << testPlayers[i].vehicleType;
	rules.agesCompleted = GetNumAgesCompleted();
	rules.difficulty = testDifficulty;

	int numPicked = 0;
	for (int i = 0; i < testNumPlayers; i++)
		if (testPlayers[i].isComputer)
			testPlayers[i].vehicleType = PickCPUVehicle(&rules, numPicked++);
}

typedef struct
{
	TestPlayer	players[MAX_TEST_PLAYERS];
	RandomCall	calls[MAX_TEST_PLAYERS];
	int			numCalls;
	uint32_t	rngState;
} PickResult;

static void RunPicker(void (*picker)(void), const TestPlayer* setup, uint32_t seed, PickResult* result)
{
	memcpy(testPlayers, setup, sizeof(testPlayers));
	rngState = seed;
	numRandomCalls = 0;
	nextCPUIndex = -1;
	picker();
	memcpy(result->players, testPlayers, sizeof(testPlayers));
	memcpy(result->calls, randomCalls, sizeof(randomCalls));
	result->numCalls = numRandomCalls;
	result->rngState = rngState;
}

static long numCompared, numOldOutOfRange;

// Runs both pickers on the current setup. Where the old picker stayed in range the
// outputs, RNG draws and final RNG state must match exactly; the new picker's output
// must satisfy the range and fairness rules everywhere.
static void CompareSetup(const TestPlayer* setup)
{
	const uint32_t seed = 0x9E3779B9u * (uint32_t)(numCompared + numOldOutOfRange + 1);
	const int best = GetBestUnlockedLandCarType(testAgesCompleted);
	PickResult oldResult, newResult;
	RunPicker(OldPickCPUVehicles, setup, seed, &oldResult);
	RunPicker(NewPickCPUVehicles, setup, seed, &newResult);

	uint32_t humanCars = 0;
	int numCPUs = 0;
	Boolean oldInRange = true;
	for (int i = 0; i < testNumPlayers; i++)
	{
		if (!setup[i].isComputer)
		{
			humanCars |= 1u << setup[i].vehicleType;
			CHECK(newResult.players[i].vehicleType == setup[i].vehicleType);	// humans keep their cars
		}
		else
		{
			numCPUs++;
			if (oldResult.players[i].vehicleType < 0 || oldResult.players[i].vehicleType > best)
				oldInRange = false;
		}
	}

	int numFree = 0;
	for (int type = 0; type <= best; type++)
		numFree += !(humanCars & (1u << type));

	// Properties of the new picker for every setup, including those the old one broke.
	int uses[NUM_LAND_CAR_TYPES] = {0};
	int cpuIndex = 0;
	for (int i = 0; i < testNumPlayers; i++)
	{
		if (!setup[i].isComputer)
			continue;
		const int type = newResult.players[i].vehicleType;
		CHECK(type >= 0 && type <= best && type < NUM_LAND_CAR_TYPES);
		if (testDifficulty == DIFFICULTY_HARD)
		{
			const RandomCall* call = &newResult.calls[cpuIndex];
			CHECK(call->min == 0 && call->max == best && call->cpuIndex == cpuIndex);
		}
		else
		{
			if (numFree > 0)
				CHECK(!(humanCars & (1u << type)));					// humans' cars only as a last resort
			if (cpuIndex < numFree)
				CHECK(uses[type] == 0);								// unique while unique cars remain
		}
		uses[type]++;
		cpuIndex++;
	}
	CHECK(newResult.numCalls == (testDifficulty == DIFFICULTY_HARD ? numCPUs : 0));

	if (!oldInRange)
	{
		numOldOutOfRange++;
		return;
	}

	numCompared++;
	CHECK(oldResult.numCalls == newResult.numCalls);
	CHECK(oldResult.rngState == newResult.rngState);
	for (int c = 0; c < oldResult.numCalls; c++)
		CHECK(oldResult.calls[c].min == newResult.calls[c].min && oldResult.calls[c].max == newResult.calls[c].max);
	for (int i = 0; i < testNumPlayers; i++)
		CHECK(oldResult.players[i].vehicleType == newResult.players[i].vehicleType);
}

// Every assignment of cars to the humans (duplicates included), for the current slots.
static void ForEachHumanCars(TestPlayer* setup, const int* humanSlots, int numHumans, int h)
{
	if (h == numHumans)
	{
		CompareSetup(setup);
		return;
	}
	for (int type = 0; type < NUM_LAND_CAR_TYPES; type++)
	{
		setup[humanSlots[h]].vehicleType = type;
		ForEachHumanCars(setup, humanSlots, numHumans, h + 1);
	}
}

static void ForEachRuleSet(TestPlayer* setup, const int* humanSlots, int numHumans)
{
	for (testAgesCompleted = 0; testAgesCompleted < NUM_TEST_AGES; testAgesCompleted++)
		for (testDifficulty = 0; testDifficulty < NUM_DIFFICULTIES; testDifficulty++)
			ForEachHumanCars(setup, humanSlots, numHumans, 0);
}

static int SetupPlayers(TestPlayer* setup, int numPlayers, uint32_t humanSlotMask, int* humanSlots)
{
	int numHumans = 0;
	memset(setup, 0, sizeof(TestPlayer) * MAX_TEST_PLAYERS);
	for (int i = 0; i < numPlayers; i++)
	{
		setup[i].isComputer = !(humanSlotMask & (1u << i));
		setup[i].vehicleType = 0x7FFF;								// CPUs must be assigned a car
		if (!setup[i].isComputer && numHumans++ < MAX_TEST_HUMANS)
			humanSlots[numHumans - 1] = i;
	}
	return numHumans;
}

// Six cars: identical output to the old picker for every human count, human slot
// layout, car assignment, age count and difficulty.
static void SixCarEquivalence(void)
{
	TestPlayer setup[MAX_TEST_PLAYERS];
	int humanSlots[MAX_TEST_HUMANS];
	testNumPlayers = 6;
	numCompared = numOldOutOfRange = 0;
	for (uint32_t layout = 0; layout < (1u << testNumPlayers); layout++)
	{
		const int numHumans = SetupPlayers(setup, testNumPlayers, layout, humanSlots);
		if (numHumans <= MAX_TEST_HUMANS)
			ForEachRuleSet(setup, humanSlots, numHumans);
	}
	CHECK(numOldOutOfRange == 0);			// six cars always fit, so every case was compared
	CHECK(numCompared == 171561L * NUM_TEST_AGES * NUM_DIFFICULTIES);
}

// Every slot count up to MAX_TEST_PLAYERS with local humans first, as in a local game.
// The old picker is still the reference wherever it stayed in range.
static void AnySlotCount(void)
{
	TestPlayer setup[MAX_TEST_PLAYERS];
	int humanSlots[MAX_TEST_HUMANS];
	numCompared = numOldOutOfRange = 0;
	for (testNumPlayers = 1; testNumPlayers <= MAX_TEST_PLAYERS; testNumPlayers++)
	{
		for (int numHumans = 0; numHumans <= MAX_TEST_HUMANS && numHumans <= testNumPlayers; numHumans++)
		{
			CHECK(SetupPlayers(setup, testNumPlayers, (1u << numHumans) - 1, humanSlots) == numHumans);
			ForEachRuleSet(setup, humanSlots, numHumans);
		}
	}
	CHECK(numCompared > 0 && numOldOutOfRange > 0);		// the old picker broke beyond six cars
}

// Direct checks of the reuse order for every set of human-held cars.
static void ReuseOrder(void)
{
	for (int ages = -1; ages <= NUM_AGES + 1; ages++)
	{
		const int best = GetBestUnlockedLandCarType(ages);
		CHECK(best == 5 + 2 * (ages < 0 ? 0 : ages > 2 ? 2 : ages));
		CHECK(best >= 0 && best < NUM_LAND_CAR_TYPES);
		for (uint32_t humanCars = 0; humanCars < (1u << NUM_LAND_CAR_TYPES); humanCars++)
		{
			for (int difficulty = 0; difficulty < DIFFICULTY_HARD; difficulty++)
			{
				const CPUVehiclePickRules rules = { .humanCarMask = humanCars, .agesCompleted = ages,
					.difficulty = difficulty };
				int freeTypes[NUM_LAND_CAR_TYPES], numFree = 0;
				for (int type = best; type >= 0; type--)
					if (!(humanCars & (1u << type)))
						freeTypes[numFree++] = type;
				for (int k = 0; k < MAX_TEST_PLAYERS; k++)
				{
					const int type = PickCPUVehicle(&rules, k);
					if (numFree > 0)
						CHECK(type == freeTypes[k % numFree]);		// unique best-first, then reuse best-first
					else
						CHECK(type == best - k % (best + 1));		// humans hold every unlocked car
				}
				CHECK(PickCPUVehicle(&rules, -1) == PickCPUVehicle(&rules, 0));
			}
		}
	}
}

static uint16_t OutOfRangeRandom(void* context, int cpuIndex, uint16_t min, uint16_t max)
{
	(void)context; (void)cpuIndex; (void)min; (void)max;
	return UINT16_MAX;
}

// Hard mode stays in range even with a misbehaving source, and without a source it
// falls back to the unique order.
static void HardModeSources(void)
{
	CPUVehiclePickRules rules = { .humanCarMask = 1u << 9, .agesCompleted = 2, .difficulty = DIFFICULTY_HARD,
		.randomRange = OutOfRangeRandom };
	for (int k = 0; k < MAX_TEST_PLAYERS; k++)
		CHECK(PickCPUVehicle(&rules, k) == 9);
	rules.randomRange = NULL;
	CHECK(PickCPUVehicle(&rules, 0) == 8 && PickCPUVehicle(&rules, 1) == 7);
}

int main(void)
{
	SixCarEquivalence();
	AnySlotCount();
	ReuseOrder();
	HardModeSources();
	puts("CPU vehicle picker tests passed");
	return 0;
}
