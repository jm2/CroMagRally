// Driver looks: default body/outfit per slot, the character screen's outfit and body
// changes, CPU re-dressing, and the minimap's repeated-outfit marker.
#include "game.h"
#include "driver_looks.h"
#include <math.h>
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


/********************* CHARACTER SCREEN CHANGES ************************/

int PositiveModulo(int value, unsigned int m)
{
	int mod = value % (int) m;
	if (mod < 0)
		mod += m;
	return mod;
}

// CycleSkin from SelectCharacter.c at f72596a, verbatim apart from reading test state
// and the six-slot table it was written for.
static void OldCycleSkin(DriverLook gPlayerInfo[ORIGINAL_NUM_PLAYERS], short whichPlayer, int delta, Boolean gNetGameInProgress)
{
			/* FIND OUT WHICH SKINS ARE ALREADY TAKEN */

	uint32_t skinsTaken = 0;

	if (!gNetGameInProgress)		// in net games, let user pick any skin
	{
		for (int prevPlayer = 0; prevPlayer < whichPlayer; prevPlayer++)
		{
			skinsTaken |= (1 << gPlayerInfo[prevPlayer].skin);
		}
	}

			/* CYCLE TO NEXT AVAILABLE SKIN */

	short oldSkin = gPlayerInfo[whichPlayer].skin;
	short newSkin = oldSkin;

	do
	{
		newSkin += delta;
		newSkin = PositiveModulo(newSkin, NUM_CAVEMAN_SKINS);
	} while (skinsTaken & (1 << newSkin));

	gPlayerInfo[whichPlayer].skin = newSkin;

			/* SWAP MY OLD SKIN W/ PLAYER THAT USES THE ONE I WANT */

	for (int i = 0; i < ORIGINAL_NUM_PLAYERS; i++)
	{
		if (i != whichPlayer && gPlayerInfo[i].skin == newSkin)
		{
			gPlayerInfo[i].skin = oldSkin;
			break;
		}
	}
}

static uint32_t PlayersBefore(int whichPlayer)
{
	return (1u << whichPlayer) - 1u;
}

static int EncodeSixSlotState(const DriverLook looks[ORIGINAL_NUM_PLAYERS])
{
	int code = 0;
	for (int i = 0; i < ORIGINAL_NUM_PLAYERS; i++)
		code = code * NUM_LOOKS + LookIndex(looks[i]);
	return code;
}

static void TestCycleDriverSkin(void)
{
	// Same answer as the old do/while whenever a skin is free; bounded when none is.
	for (int skin = 0; skin < NUM_CAVEMAN_SKINS; skin++)
	{
		for (uint32_t taken = 0; taken < (1u << NUM_CAVEMAN_SKINS); taken++)
		{
			for (int delta = -1; delta <= 1; delta += 2)
			{
				short expected = (short) skin;
				if (taken != (1u << NUM_CAVEMAN_SKINS) - 1)
				{
					do
						expected = (short) PositiveModulo(expected + delta, NUM_CAVEMAN_SKINS);
					while (taken & (1u << expected));
				}
				else
				{
					expected = (short) PositiveModulo(skin + delta, NUM_CAVEMAN_SKINS);
				}
				CHECK(CycleDriverSkin((short) skin, delta, taken) == expected);
			}
		}
	}
}

// Every six-car state the character screens can reach: local players 0...3 cycle
// outfits and pick bodies, in any order (the screens allow going back).
static void TestSixCarScreensMatchOriginal(void)
{
	enum { NUM_STATES = NUM_LOOKS * NUM_LOOKS * NUM_LOOKS * NUM_LOOKS * NUM_LOOKS * NUM_LOOKS };
	unsigned char* seen = calloc(NUM_STATES, 1);
	DriverLook* queue = malloc(sizeof(DriverLook) * ORIGINAL_NUM_PLAYERS * 200000);
	int head = 0, tail = 0, numStates = 0;
	CHECK(seen && queue);

	DriverLook start[ORIGINAL_NUM_PLAYERS];
	GetDefaultLooks(start, ORIGINAL_NUM_PLAYERS);
	seen[EncodeSixSlotState(start)] = 1;
	memcpy(&queue[tail++ * ORIGINAL_NUM_PLAYERS], start, sizeof(start));

	while (head < tail)
	{
		const DriverLook* state = &queue[head++ * ORIGINAL_NUM_PLAYERS];
		numStates++;
		CHECK(AllSkinsDistinct(state, ORIGINAL_NUM_PLAYERS));

		for (int who = 0; who < MAX_TEST_HUMANS; who++)
		{
			for (int op = 0; op < 5; op++)				// cycle up, cycle down, pick Brog, pick Grag, enter screen
			{
				DriverLook oldRules[ORIGINAL_NUM_PLAYERS], newRules[ORIGINAL_NUM_PLAYERS];
				memcpy(oldRules, state, sizeof(oldRules));
				memcpy(newRules, state, sizeof(newRules));

				if (op < 2)
				{
					const int delta = op == 0 ? 1 : -1;
					OldCycleSkin(oldRules, (short) who, delta, false);
					CHECK(CycleDriverOutfit(newRules, ORIGINAL_NUM_PLAYERS, who, delta, PlayersBefore(who), true)
						== oldRules[who].skin);
				}
				else if (op < 4)
				{
					oldRules[who].sex = (short) (op - 2);		// the old screen just stored the body
					ChangeDriverBody(newRules, ORIGINAL_NUM_PLAYERS, who, (short) (op - 2), PlayersBefore(who), true);
				}
				else											// the old screen kept whatever outfit it found
				{
					CHECK(CycleDriverOutfit(newRules, ORIGINAL_NUM_PLAYERS, who, 0, PlayersBefore(who), true)
						== oldRules[who].skin);
				}
				CHECK(memcmp(oldRules, newRules, sizeof(oldRules)) == 0);

				const int code = EncodeSixSlotState(newRules);
				if (!seen[code])
				{
					seen[code] = 1;
					CHECK(tail < 200000);
					memcpy(&queue[tail++ * ORIGINAL_NUM_PLAYERS], newRules, sizeof(newRules));
				}

					/* NETWORK: SAME OUTFIT FOR ME, NOBODY ELSE TOUCHED */

				if (op < 2)
				{
					DriverLook oldNet[ORIGINAL_NUM_PLAYERS], newNet[ORIGINAL_NUM_PLAYERS];
					memcpy(oldNet, state, sizeof(oldNet));
					memcpy(newNet, state, sizeof(newNet));
					OldCycleSkin(oldNet, (short) who, op == 0 ? 1 : -1, true);
					CycleDriverOutfit(newNet, ORIGINAL_NUM_PLAYERS, who, op == 0 ? 1 : -1, 0, false);
					for (int i = 0; i < ORIGINAL_NUM_PLAYERS; i++)
					{
						CHECK(newNet[i].sex == state[i].sex);
						CHECK(newNet[i].skin == (i == who ? oldNet[who].skin : state[i].skin));
					}
				}
			}
		}
	}

	CHECK(numStates > 1000);
	free(queue);
	free(seen);
}

static void TestTwelveCarScreens(void)
{
	// The old swap handed my outfit to the first match only, so the second wave's copy
	// of that outfit now repeats a look.
	DriverLook looks[NUM_LOOKS];
	GetDefaultLooks(looks, NUM_LOOKS);
	{
		DriverLook sixSlots[ORIGINAL_NUM_PLAYERS];
		memcpy(sixSlots, looks, sizeof(sixSlots));
		OldCycleSkin(sixSlots, 0, 1, false);		// first six: player 1 takes brown as a Grag
		memcpy(looks, sixSlots, sizeof(sixSlots));
		CHECK(!AllLooksDistinct(looks, NUM_LOOKS));	// ...which slot 6 already wears
	}

	// With the new rules every look stays distinct through any sequence of local changes,
	// and players who chose earlier never change.
	for (int trial = 0; trial < 2000; trial++)
	{
		GetDefaultLooks(looks, NUM_LOOKS);
		for (int step = 0; step < 40; step++)
		{
			const int who = NextRandom(MAX_TEST_HUMANS);
			DriverLook before[NUM_LOOKS];

			CycleDriverOutfit(looks, NUM_LOOKS, who, 0, PlayersBefore(who), true);	// entering the screen
			CHECK(AllLooksDistinct(looks, NUM_LOOKS));
			for (int prev = 0; prev < who; prev++)
				CHECK(looks[who].skin != looks[prev].skin);
			memcpy(before, looks, sizeof(looks));

			if (NextRandom(2))
			{
				const short newSkin = CycleDriverOutfit(looks, NUM_LOOKS, who, NextRandom(2) ? 1 : -1, PlayersBefore(who), true);
				CHECK(looks[who].skin == newSkin);
				CHECK(looks[who].sex == before[who].sex);
				for (int prev = 0; prev < who; prev++)
					CHECK(newSkin != before[prev].skin);			// earlier players' outfits are skipped
			}
			else
			{
				const short sex = (short) NextRandom(2);
				ChangeDriverBody(looks, NUM_LOOKS, who, sex, PlayersBefore(who), true);
				CHECK(looks[who].sex == sex && looks[who].skin == before[who].skin);
			}

			CHECK(AllLooksDistinct(looks, NUM_LOOKS));
			for (int prev = 0; prev < who; prev++)
				CHECK(LookIndex(looks[prev]) == LookIndex(before[prev]));
		}
	}

	// Seven players who chose already hold every outfit between them: cycling still moves
	// on (it used to spin forever), skips only their looks with my body, and never changes them.
	GetDefaultLooks(looks, NUM_LOOKS);
	const short next = CycleDriverOutfit(looks, NUM_LOOKS, 7, 1, PlayersBefore(7), true);
	CHECK(next == 3);												// Brog in outfit 2 is player 2's look
	CHECK(looks[7].sex == 0 && looks[7].skin == 3);
	CHECK(AllLooksDistinct(looks, NUM_LOOKS));
	for (int prev = 0; prev < 7; prev++)
		CHECK(LookIndex(looks[prev]) == LookIndex(GetDefaultDriverLook(prev)));
	CHECK(CycleDriverSkin(2, 1, (1u << NUM_CAVEMAN_SKINS) - 1) == 3);	// everything taken: bounded

	// Entering the screen (delta 0) keeps a free outfit, and moves off one a player
	// before me took since I was dressed.
	GetDefaultLooks(looks, NUM_LOOKS);
	CHECK(CycleDriverOutfit(looks, NUM_LOOKS, 3, 0, PlayersBefore(3), true) == 3);
	CHECK(LookIndex(looks[3]) == LookIndex(GetDefaultDriverLook(3)));
	CycleDriverOutfit(looks, NUM_LOOKS, 0, 1, 0, true);				// player 0 cycles Brog 0 -> 1
	CycleDriverOutfit(looks, NUM_LOOKS, 0, 1, 0, true);				// ...-> 2
	CycleDriverOutfit(looks, NUM_LOOKS, 0, 1, 0, true);				// ...-> 3, which player 3 wears as Grag
	CHECK(looks[0].skin == 3 && looks[3].skin == 3);
	CHECK(CycleDriverOutfit(looks, NUM_LOOKS, 3, 0, PlayersBefore(3), true) == 4);
	CHECK(AllLooksDistinct(looks, NUM_LOOKS));
	ChangeDriverBody(looks, NUM_LOOKS, 3, 0, PlayersBefore(3), true);	// so a later body change can't copy player 0
	CHECK(AllLooksDistinct(looks, NUM_LOOKS));

	// A network player changes only its own look, whatever the others wear.
	GetDefaultLooks(looks, NUM_LOOKS);
	for (int step = 0; step < 100; step++)
	{
		DriverLook before[NUM_LOOKS];
		memcpy(before, looks, sizeof(looks));
		const int who = NextRandom(NUM_LOOKS);
		if (step & 1)
			CycleDriverOutfit(looks, NUM_LOOKS, who, 1, 0, false);
		else
			ChangeDriverBody(looks, NUM_LOOKS, who, (short) NextRandom(2), 0, false);
		for (int i = 0; i < NUM_LOOKS; i++)
			CHECK(i == who || LookIndex(looks[i]) == LookIndex(before[i]));
	}

	// Out-of-range players are ignored.
	GetDefaultLooks(looks, NUM_LOOKS);
	CHECK(SwapDriverLook(looks, NUM_LOOKS, NUM_LOOKS, looks[0], 0) == -1);
	ChangeDriverBody(looks, NUM_LOOKS, -1, 1, 0, true);
	CHECK(AllLooksDistinct(looks, NUM_LOOKS));
}


/********************* MINIMAP BLIP COLORS ************************/

static void TestBlipColors(void)
{
	DriverLook looks[MAX_TEST_PLAYERS];

	// Six cars: nobody repeats an outfit, so every blip looks as it always did.
	GetDefaultLooks(looks, ORIGINAL_NUM_PLAYERS);
	for (int i = 0; i < ORIGINAL_NUM_PLAYERS; i++)
		CHECK(GetDriverOutfitRank(looks, i) == 0);

	// Twelve cars: the first six keep their colours and the second wave takes partner colours.
	GetDefaultLooks(looks, NUM_LOOKS);
	for (int i = 0; i < NUM_LOOKS; i++)
		CHECK(GetDriverOutfitRank(looks, i) == (i < ORIGINAL_NUM_PLAYERS ? 0 : 1));

	// Whatever the humans choose, each outfit has exactly one plain blip.
	for (int trial = 0; trial < 5000; trial++)
	{
		GetDefaultLooks(looks, NUM_LOOKS);
		for (int step = 0; step < 20; step++)
		{
			const int who = NextRandom(MAX_TEST_HUMANS);
			CycleDriverOutfit(looks, NUM_LOOKS, who, NextRandom(2) ? 1 : -1, PlayersBefore(who), true);
		}
		int plain[NUM_CAVEMAN_SKINS] = {0}, marked = 0;
		for (int i = 0; i < NUM_LOOKS; i++)
		{
			const int rank = GetDriverOutfitRank(looks, i);
			CHECK(rank == 0 || rank == 1);
			if (rank == 0)
				plain[looks[i].skin]++;
			else
				marked++;
		}
		for (int skin = 0; skin < NUM_CAVEMAN_SKINS; skin++)
			CHECK(plain[skin] == 1);
		CHECK(marked == NUM_LOOKS - NUM_CAVEMAN_SKINS);
	}

	// First wearers keep the original colours; later wearers take the partner colour. Every
	// new colour is at least 0.5 (RGB distance) from all eleven others, further apart than the
	// closest original pair (brown and gray, 0.36), and 0.75 from its partner.
	OGLColorRGB colors[2 * NUM_CAVEMAN_SKINS];
	for (int skin = 0; skin < NUM_CAVEMAN_SKINS; skin++)
	{
		const OGLColorRGB first = GetDriverBlipColor(skin, 0), second = GetDriverBlipColor(skin, 1);
		CHECK(!memcmp(&first, &kCavemanSkinColors[skin], sizeof(first)));
		CHECK(!memcmp(&second, &kRepeatOutfitColors[skin], sizeof(second)));
		const OGLColorRGB third = GetDriverBlipColor(skin, 2);
		CHECK(!memcmp(&third, &second, sizeof(third)));
		colors[skin] = first;
		colors[NUM_CAVEMAN_SKINS + skin] = second;
	}
	for (int a = NUM_CAVEMAN_SKINS; a < 2 * NUM_CAVEMAN_SKINS; a++)
		for (int b = 0; b < 2 * NUM_CAVEMAN_SKINS; b++)
		{
			if (a == b)
				continue;
			const float dr = colors[a].r - colors[b].r, dg = colors[a].g - colors[b].g, db = colors[a].b - colors[b].b;
			CHECK(dr * dr + dg * dg + db * db >= 0.25f);
			if (b == a - NUM_CAVEMAN_SKINS)
				CHECK(dr * dr + dg * dg + db * db >= 0.5f);
		}
}


int main(void)
{
	TestDefaultLooks();
	TestResolveCPUDriverLooks();
	TestCycleDriverSkin();
	TestSixCarScreensMatchOriginal();
	TestTwelveCarScreens();
	TestBlipColors();
	puts("driver looks tests passed");
	return 0;
}
