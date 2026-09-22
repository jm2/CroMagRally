// Driver looks: default body/outfit per slot and CPU re-dressing.
#include "game.h"
#include "driver_looks.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) do { if (!(condition)) { \
	fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); \
	exit(EXIT_FAILURE); } } while (0)

#define NUM_LOOKS				(NUM_DRIVER_SEXES * NUM_CAVEMAN_SKINS)
#define ORIGINAL_NUM_PLAYERS	6			// the six-car grid the original rules were written for
#define MAX_TEST_PLAYERS		16			// beyond twelve, where repeats are unavoidable
#define MAX_TEST_HUMANS			4

_Static_assert(NUM_LOOKS == 12, "two bodies x six outfits");
_Static_assert(MAX_TEST_PLAYERS >= MAX_PLAYERS, "cover every supported slot count");

void DoFatalAlert(const char* format, ...) { (void)format; abort(); }

static uint32_t rngState = 12345;
static int NextRandom(int n)
{
	rngState = rngState * 1664525u + 1013904223u;
	return (int) ((rngState >> 8) % (uint32_t) n);
}

static int LookIndex(DriverLook look)
{
	CHECK(look.sex >= 0 && look.sex < NUM_DRIVER_SEXES);
	CHECK(look.skin >= 0 && look.skin < NUM_CAVEMAN_SKINS);
	return look.sex * NUM_CAVEMAN_SKINS + look.skin;
}

static void GetDefaultLooks(DriverLook looks[], int numPlayers)
{
	for (int i = 0; i < numPlayers; i++)
		looks[i] = GetDefaultDriverLook(i);
}

static Boolean AllLooksDistinct(const DriverLook looks[], int numPlayers)
{
	int used[NUM_LOOKS] = {0};
	for (int i = 0; i < numPlayers; i++)
	{
		if (used[LookIndex(looks[i])]++)
			return false;
	}
	return true;
}

static Boolean AllSkinsDistinct(const DriverLook looks[], int numPlayers)
{
	int used[NUM_CAVEMAN_SKINS] = {0};
	for (int i = 0; i < numPlayers; i++)
	{
		if (used[looks[i].skin]++)
			return false;
	}
	return true;
}


/************************ DEFAULT LOOKS ***************************/

static void TestDefaultLooks(void)
{
	DriverLook looks[MAX_TEST_PLAYERS];
	GetDefaultLooks(looks, MAX_TEST_PLAYERS);

	// The original six slots alternate male/female and wear every outfit once.
	for (int i = 0; i < ORIGINAL_NUM_PLAYERS; i++)
	{
		CHECK(looks[i].sex == (i & 1));
		CHECK(looks[i].skin == i % NUM_CAVEMAN_SKINS);
	}

	// Every slot up to twelve looks different, and the charter's formula holds.
	for (int n = 1; n <= NUM_LOOKS; n++)
		CHECK(AllLooksDistinct(looks, n));
	for (int i = 0; i < MAX_TEST_PLAYERS; i++)
		CHECK(looks[i].sex == ((i & 1) ^ ((i / NUM_CAVEMAN_SKINS) & 1)));

	// Past twelve, each look repeats as evenly as possible.
	int used[NUM_LOOKS] = {0};
	for (int i = 0; i < MAX_TEST_PLAYERS; i++)
		CHECK(++used[LookIndex(looks[i])] <= (i / NUM_LOOKS) + 1);

	// Humans start on Brog, as in the original game, except in the second wave, so
	// twelve humans who keep the defaults on a LAN still look different.
	DriverLook humans[NUM_LOOKS];
	for (int i = 0; i < NUM_LOOKS; i++)
	{
		CHECK(GetDefaultHumanDriverSex(i) == (i < ORIGINAL_NUM_PLAYERS ? 0 : 1));
		humans[i] = (DriverLook) { GetDefaultHumanDriverSex(i), (short) (i % NUM_CAVEMAN_SKINS) };
	}
	CHECK(AllLooksDistinct(humans, NUM_LOOKS));
	CHECK(GetDefaultDriverLook(-1).skin == 0);
	CHECK(GetDefaultHumanDriverSex(-1) == 0);
}


/********************* RESOLVE CPU DRIVER LOOKS ************************/

static void CheckResolved(const DriverLook before[], const DriverLook after[], int numPlayers, int numFixed)
{
	for (int i = 0; i < numFixed; i++)							// humans keep what they chose
	{
		CHECK(after[i].sex == before[i].sex);
		CHECK(after[i].skin == before[i].skin);
	}

	int lookUse[NUM_LOOKS] = {0};
	for (int i = 0; i < numPlayers; i++)
		lookUse[LookIndex(after[i])]++;

	Boolean someLookFree = false;
	for (int k = 0; k < NUM_LOOKS; k++)
	{
		if (lookUse[k] == 0)
			someLookFree = true;
	}

	// A CPU repeats someone only when every look is taken (so never with twelve or fewer).
	for (int i = numFixed; i < numPlayers; i++)
		CHECK(!someLookFree || lookUse[LookIndex(after[i])] == 1);

	if (numPlayers <= NUM_LOOKS)								// settled: a second pass changes nothing
	{
		DriverLook again[NUM_LOOKS];
		memcpy(again, after, sizeof(DriverLook) * numPlayers);
		CHECK(ResolveCPUDriverLooks(again, numPlayers, numFixed) == 0);
		CHECK(memcmp(again, after, sizeof(DriverLook) * numPlayers) == 0);
	}
}

static void TestResolveCPUDriverLooks(void)
{
	// Defaults, and any grid whose outfits are all different (every local game at six
	// cars), are left exactly as they are.
	for (int n = 0; n <= NUM_LOOKS; n++)
	{
		for (int fixed = 0; fixed <= n && fixed <= MAX_TEST_HUMANS; fixed++)
		{
			DriverLook looks[NUM_LOOKS], before[NUM_LOOKS];
			GetDefaultLooks(looks, n);
			memcpy(before, looks, sizeof(looks));
			CHECK(ResolveCPUDriverLooks(looks, n, fixed) == 0);
			CHECK(memcmp(before, looks, sizeof(DriverLook) * n) == 0);
		}
	}

	for (int trial = 0; trial < 20000; trial++)
	{
		DriverLook looks[ORIGINAL_NUM_PLAYERS], before[ORIGINAL_NUM_PLAYERS];
		for (int i = 0; i < ORIGINAL_NUM_PLAYERS; i++)
			looks[i] = (DriverLook) { (short) NextRandom(2), (short) i };
		for (int i = ORIGINAL_NUM_PLAYERS - 1; i > 0; i--)		// shuffle the outfits
		{
			int j = NextRandom(i + 1);
			short t = looks[i].skin; looks[i].skin = looks[j].skin; looks[j].skin = t;
		}
		memcpy(before, looks, sizeof(looks));
		CHECK(ResolveCPUDriverLooks(looks, ORIGINAL_NUM_PLAYERS, NextRandom(MAX_TEST_HUMANS + 1)) == 0);
		CHECK(memcmp(before, looks, sizeof(looks)) == 0);
	}

	// A human's choice (network games choose freely, and humans may even match each
	// other) pushes the CPU wearing it onto a free look; with six cars the CPUs also
	// keep every outfit different while one is free.
	for (int trial = 0; trial < 50000; trial++)
	{
		const int n = 1 + NextRandom(MAX_TEST_PLAYERS);
		const int fixed = NextRandom((n < MAX_TEST_HUMANS ? n : MAX_TEST_HUMANS) + 1);
		DriverLook looks[MAX_TEST_PLAYERS], before[MAX_TEST_PLAYERS];
		GetDefaultLooks(looks, n);
		for (int i = 0; i < fixed; i++)
			looks[i] = (DriverLook) { (short) NextRandom(2), (short) NextRandom(NUM_CAVEMAN_SKINS) };
		if (trial % 3 == 0)										// and any leftover CPU looks
		{
			for (int i = fixed; i < n; i++)
				looks[i] = (DriverLook) { (short) NextRandom(2), (short) NextRandom(NUM_CAVEMAN_SKINS) };
		}
		memcpy(before, looks, sizeof(looks));
		ResolveCPUDriverLooks(looks, n, fixed);
		CheckResolved(before, looks, n, fixed);

		if (n <= NUM_CAVEMAN_SKINS && AllSkinsDistinct(before, fixed))
			CHECK(AllSkinsDistinct(looks, n));

		DriverLook replay[MAX_TEST_PLAYERS];					// a pure function: every peer agrees
		memcpy(replay, before, sizeof(before));
		ResolveCPUDriverLooks(replay, n, fixed);
		CHECK(memcmp(replay, looks, sizeof(DriverLook) * n) == 0);
	}

	// Example: a LAN human picks CPU 4's look; that CPU takes the only free one.
	DriverLook looks[NUM_LOOKS];
	GetDefaultLooks(looks, NUM_LOOKS);
	looks[1] = looks[4];
	CHECK(ResolveCPUDriverLooks(looks, NUM_LOOKS, 2) == 1);
	CHECK(looks[4].sex == 1 && looks[4].skin == 1);

	// A CPU with an out-of-range look is re-dressed validly.
	GetDefaultLooks(looks, NUM_LOOKS);
	looks[7] = (DriverLook) { 5, -3 };
	CHECK(ResolveCPUDriverLooks(looks, NUM_LOOKS, 1) == 1);
	CHECK(AllLooksDistinct(looks, NUM_LOOKS));
}


int main(void)
{
	TestDefaultLooks();
	TestResolveCPUDriverLooks();
	puts("driver looks tests passed");
	return 0;
}
