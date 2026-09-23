#include "game.h"
#include "cpu_fill.h"
#include "driver_looks.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) do { if (!(condition)) { \
	fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); \
	exit(EXIT_FAILURE); } } while (0)

static PlayerInfoType players[MAX_PLAYERS];
static short numPlayers;

// Humans take slots 0..numHumans-1 and CPUs the rest, as InitPlayerInfo_Game seats them.
static void SeatPlayers(short numHumans, short total)
{
	memset(players, 0, sizeof(players));
	numPlayers = total;
	for (short p = 0; p < total; p++)
		players[p].isComputer = p >= numHumans;
}

// Crosses the line in the given order the way PlayerCompletedRace does, one finish at a
// time (same-frame finishes are handled in player order too). Returns the winner, or -1
// if no finish ended the race. results receives the deciding finish's results.
static short RunFinishes(const short* order, int numFinishes, Byte results[MAX_PLAYERS])
{
	short winner = -1;
	memset(results, 0xFF, MAX_PLAYERS);
	for (int i = 0; i < numFinishes; i++)
	{
		Byte scratch[MAX_PLAYERS];
		Boolean decided = winner >= 0;
		if (DecideMultiplayerRaceFinish(players, numPlayers, order[i], decided, decided ? scratch : results))
		{
			CHECK(!decided);
			winner = order[i];
		}
	}
	return winner;
}

static void TestModes(void)
{
	for (int mode = 0; mode < NUM_GAME_MODES; mode++)
		CHECK(CPUFillAppliesToMode(mode) == (mode == GAME_MODE_MULTIPLAYERRACE));
}

// A local game takes this machine's pref; a network game only the host's config.
static void TestFillDecision(void)
{
	for (int mode = 0; mode < NUM_GAME_MODES; mode++)
	{
		for (int bits = 0; bits < 8; bits++)
		{
			Boolean netGame = bits & 1, hostConfig = (bits >> 1) & 1, pref = (bits >> 2) & 1;
			Boolean expected = mode == GAME_MODE_MULTIPLAYERRACE && (netGame ? hostConfig : pref);
			CHECK(DecideCPUFillThisRace(mode, netGame, hostConfig, pref) == expected);
		}
	}
}

// Only the original 6 and every slot this build has are offered.
static void TestPlayerLimits(void)
{
	for (int limit = -1; limit <= 256; limit++)
		CHECK(IS_SUPPORTED_PLAYER_LIMIT(limit) == (limit == PLAYER_LIMIT_ORIGINAL || limit == MAX_PLAYERS));
	CHECK(IS_SUPPORTED_PLAYER_LIMIT(6));
	CHECK(IS_SUPPORTED_PLAYER_LIMIT(MAX_PLAYERS));
	CHECK(!IS_SUPPORTED_PLAYER_LIMIT(0) && !IS_SUPPORTED_PLAYER_LIMIT(7) && !IS_SUPPORTED_PLAYER_LIMIT(MAX_PLAYERS + 1));

	// A local game takes this machine's pref; a network game only the host's config.
	// Unsupported values (never from sanitized prefs or a validated config) fall back to 6.
	static const int kValues[] = { 0, 1, PLAYER_LIMIT_ORIGINAL, 7, MAX_PLAYERS, MAX_PLAYERS + 1, 255 };
	const int numValues = (int) (sizeof(kValues) / sizeof(kValues[0]));
	for (int netGame = 0; netGame <= 1; netGame++)
	{
		for (int h = 0; h < numValues; h++)
		{
			for (int p = 0; p < numValues; p++)
			{
				int chosen = netGame ? kValues[h] : kValues[p];
				int expected = IS_SUPPORTED_PLAYER_LIMIT(chosen) ? chosen : PLAYER_LIMIT_ORIGINAL;
				CHECK(DecidePlayerLimitThisGame(netGame, kValues[h], kValues[p]) == expected);
			}
		}
	}
	CHECK(DecidePlayerLimitThisGame(false, PLAYER_LIMIT_ORIGINAL, MAX_PLAYERS) == MAX_PLAYERS);	// a host's pref
	CHECK(DecidePlayerLimitThisGame(true, PLAYER_LIMIT_ORIGINAL, MAX_PLAYERS) == PLAYER_LIMIT_ORIGINAL);	// a client's
	CHECK(DecidePlayerLimitThisGame(true, MAX_PLAYERS, PLAYER_LIMIT_ORIGINAL) == MAX_PLAYERS);

	// A smoke host seats the smallest supported limit that holds its players.
	for (int players = 1; players <= MAX_PLAYERS; players++)
	{
		Byte limit = SmallestPlayerLimitFor(players);
		CHECK(IS_SUPPORTED_PLAYER_LIMIT(limit) && limit >= players);
		CHECK(limit == PLAYER_LIMIT_ORIGINAL || players > PLAYER_LIMIT_ORIGINAL);
	}
	CHECK(SmallestPlayerLimitFor(2) == PLAYER_LIMIT_ORIGINAL && SmallestPlayerLimitFor(6) == PLAYER_LIMIT_ORIGINAL);
	CHECK(SmallestPlayerLimitFor(7) == MAX_PLAYERS && SmallestPlayerLimitFor(MAX_PLAYERS) == MAX_PLAYERS);
}

static void TestPlayerCounts(void)
{
	static const short kLimits[] = { PLAYER_LIMIT_ORIGINAL, MAX_PLAYERS };
	for (int l = 0; l < 2; l++)
	{
		const short limit = kLimits[l];
		for (int mode = 0; mode < NUM_GAME_MODES; mode++)
		{
			for (short humans = 1; humans <= MAX_PLAYERS; humans++)
			{
				const short fullGrid = humans > limit ? humans : limit;	// every human races
				for (int fill = 0; fill <= 1; fill++)
				{
					short count = CountPlayersInGame(mode, humans, fill, limit);
					switch (mode)
					{
						case GAME_MODE_PRACTICE:					// single-player races use every slot
						case GAME_MODE_TOURNAMENT:
							CHECK(count == fullGrid);
							break;

						case GAME_MODE_MULTIPLAYERRACE:				// CPU cars only with fill
							CHECK(count == (fill ? fullGrid : humans));
							break;

						default:									// battle modes: humans only
							CHECK(count == humans);
							break;
					}
				}
			}
		}
	}

	CHECK(CountPlayersInGame(GAME_MODE_PRACTICE, 1, false, PLAYER_LIMIT_ORIGINAL) == 6);		// the original grid
	CHECK(CountPlayersInGame(GAME_MODE_TOURNAMENT, 1, false, MAX_PLAYERS) == MAX_PLAYERS);
	CHECK(CountPlayersInGame(GAME_MODE_MULTIPLAYERRACE, 2, true, PLAYER_LIMIT_ORIGINAL) == 6);
	CHECK(CountPlayersInGame(GAME_MODE_MULTIPLAYERRACE, 2, true, MAX_PLAYERS) == MAX_PLAYERS);
	CHECK(CountPlayersInGame(GAME_MODE_MULTIPLAYERRACE, 2, false, MAX_PLAYERS) == 2);
	CHECK(CountPlayersInGame(GAME_MODE_SURVIVAL, 3, true, MAX_PLAYERS) == 3);
}

#define NUM_LOOKS			(2 * NUM_CAVEMAN_SKINS)
#define MAX_LOOK_PLAYERS	16								// beyond MAX_PLAYERS and the number of looks

static PlayerInfoType lookPlayers[MAX_LOOK_PLAYERS];

static int LookOf(const PlayerInfoType* player)
{
	CHECK(player->sex >= 0 && player->sex <= 1 && player->skin >= 0 && player->skin < NUM_CAVEMAN_SKINS);
	return player->sex * NUM_CAVEMAN_SKINS + player->skin;
}

static int CountBits(uint32_t bits)
{
	int n = 0;
	for (; bits; bits &= bits - 1)
		n++;
	return n;
}

// Runs MakeCPULooksDistinct and checks its promises. Returns how many CPUs changed look.
static int CheckDistinctLooks(short total)
{
	PlayerInfoType before[MAX_LOOK_PLAYERS];
	memcpy(before, lookPlayers, sizeof(before));
	MakeCPULooksDistinct(lookPlayers, total);

	int changed = 0, cpus = 0;
	uint32_t humansWear = 0, cpusWear = 0;
	for (short p = 0; p < total; p++)
	{
		int look = LookOf(&lookPlayers[p]);							// every look is a valid one
		if (!lookPlayers[p].isComputer)
		{
			CHECK(lookPlayers[p].sex == before[p].sex && lookPlayers[p].skin == before[p].skin);	// humans keep theirs
			humansWear |= 1u << look;
		}
		else
		{
			cpus++;
			cpusWear |= 1u << look;
			if (lookPlayers[p].sex != before[p].sex || lookPlayers[p].skin != before[p].skin)
				changed++;
		}
	}
	if (CountBits(humansWear) + cpus <= NUM_LOOKS)					// enough looks: every CPU has its own,
		CHECK(CountBits(cpusWear) == cpus && !(cpusWear & humansWear));	// and none looks like a human
	for (short p = total; p < MAX_LOOK_PLAYERS; p++)				// players past the count are untouched
		CHECK(memcmp(&lookPlayers[p], &before[p], sizeof(before[p])) == 0);
	return changed;
}

static void TestDriverLooks(void)
{
	// Up to one player per skin, as InitPlayerInfo_Game and CycleSkin keep them today,
	// every look is already distinct whatever sex each player picked: nothing changes.
	for (short total = 1; total <= NUM_CAVEMAN_SKINS; total++)
		for (short humans = 1; humans <= total && humans <= MAX_LOCAL_PLAYERS; humans++)
			for (int rotation = 0; rotation < NUM_CAVEMAN_SKINS; rotation++)
				for (uint32_t sexes = 0; sexes < (1u << total); sexes++)
				{
					memset(lookPlayers, 0, sizeof(lookPlayers));
					for (short p = 0; p < total; p++)
					{
						lookPlayers[p].isComputer = p >= humans;
						lookPlayers[p].sex = (sexes >> p) & 1;
						lookPlayers[p].skin = (p + rotation) % NUM_CAVEMAN_SKINS;
					}
					CHECK(CheckDistinctLooks(total) == 0);
				}

	// A 12-car grid starting from all 12 looks: a human picking a CPU's look pushes that
	// CPU to the other sex in its skin, or to another free look.
	memset(lookPlayers, 0, sizeof(lookPlayers));
	for (short p = 0; p < NUM_LOOKS; p++)
	{
		lookPlayers[p].isComputer = p >= 2;
		lookPlayers[p].sex = (p & 1) ^ ((p / NUM_CAVEMAN_SKINS) & 1);
		lookPlayers[p].skin = p % NUM_CAVEMAN_SKINS;
	}
	lookPlayers[0].sex = 1;											// player 0 now looks like player 6
	lookPlayers[1].skin = 2;										// and player 1 like player 8
	CHECK(CheckDistinctLooks(NUM_LOOKS) == 2);						// no other CPU moves
	CHECK(lookPlayers[6].sex == 0 && lookPlayers[6].skin == 0);		// the other sex in its skin
	CHECK(lookPlayers[8].sex == 1 && lookPlayers[8].skin == 1);		// the one look left

	// With looks to spare, a CPU keeps its skin (its minimap colour) if it can.
	memset(lookPlayers, 0, sizeof(lookPlayers));
	for (short p = 0; p < 8; p++)
	{
		lookPlayers[p].isComputer = p >= 2;
		lookPlayers[p].sex = (p & 1) ^ ((p / NUM_CAVEMAN_SKINS) & 1);
		lookPlayers[p].skin = p % NUM_CAVEMAN_SKINS;
	}
	lookPlayers[1].sex = 0;											// player 1 now looks like player 7
	CHECK(CheckDistinctLooks(8) == 1);
	CHECK(lookPlayers[7].sex == 1 && lookPlayers[7].skin == 1);

	// Pseudo-random grids up to more players than looks, including humans wearing the
	// same look (network players may) and CPUs with no valid look yet.
	uint32_t seed = 12345;
	for (int round = 0; round < 20000; round++)
	{
		memset(lookPlayers, 0, sizeof(lookPlayers));
		seed = seed * 1664525u + 1013904223u;
		short total = 1 + (seed >> 8) % MAX_LOOK_PLAYERS;
		short humans = 1 + (seed >> 16) % total;
		for (short p = 0; p < total; p++)
		{
			seed = seed * 1664525u + 1013904223u;
			lookPlayers[p].isComputer = p >= humans;
			lookPlayers[p].sex = (seed >> 8) & 1;
			lookPlayers[p].skin = (seed >> 9) % NUM_CAVEMAN_SKINS;
		}
		if (total > humans && (seed >> 20) % 4 == 0)
			lookPlayers[total - 1].skin = NUM_CAVEMAN_SKINS;		// no such skin
		CheckDistinctLooks(total);
	}
}

// Network fill CPUs: whatever a character screen left in their slots on this machine,
// every peer puts them back in their dealt looks (ResolveCPUDriverLooks, tested with the
// driver looks, then keeps them apart). Human slots keep their looks, including players
// who left since (bots).
static void TestNetworkFillLooks(void)
{
	uint32_t rng = 12345;
	#define NEXT_RANDOM()	(rng = rng * 1664525u + 1013904223u, rng >> 8)

	for (short humans = 1; humans < MAX_LOOK_PLAYERS; humans++)
	{
		for (int trial = 0; trial < 200; trial++)
		{
			PlayerInfoType peer[2][MAX_LOOK_PLAYERS];
			memset(peer, 0, sizeof(peer));
			for (short p = 0; p < humans; p++)								// network humans may repeat looks
			{
				peer[0][p].sex = (short) (NEXT_RANDOM() & 1);
				peer[0][p].skin = (short) (NEXT_RANDOM() % NUM_CAVEMAN_SKINS);
				peer[0][p].isComputer = (NEXT_RANDOM() % 4) == 0;			// left since, maybe seen on one peer only
				peer[1][p] = peer[0][p];
				peer[1][p].isComputer = !peer[0][p].isComputer;
			}
			for (int view = 0; view < 2; view++)							// each peer's screen swapped differently
			{
				for (short p = humans; p < MAX_LOOK_PLAYERS; p++)
				{
					peer[view][p].isComputer = true;
					peer[view][p].sex = (short) (NEXT_RANDOM() & 1);
					peer[view][p].skin = (short) (NEXT_RANDOM() % NUM_CAVEMAN_SKINS);
				}
			}

			for (int view = 0; view < 2; view++)
			{
				PlayerInfoType before[MAX_LOOK_PLAYERS];
				memcpy(before, peer[view], sizeof(before));
				DressNetworkFillCPUs(peer[view], humans, MAX_LOOK_PLAYERS);
				for (short p = 0; p < MAX_LOOK_PLAYERS; p++)
				{
					const DriverLook dealt = GetDefaultDriverLook(p);
					CHECK(peer[view][p].isComputer == before[p].isComputer);
					if (p < humans)
						CHECK(peer[view][p].sex == before[p].sex && peer[view][p].skin == before[p].skin);
					else
						CHECK(peer[view][p].sex == dealt.sex && peer[view][p].skin == dealt.skin);
				}
			}
			for (short p = 0; p < MAX_LOOK_PLAYERS; p++)						// so both peers agree
				CHECK(peer[0][p].sex == peer[1][p].sex && peer[0][p].skin == peer[1][p].skin);
		}
	}

	#undef NEXT_RANDOM
}

static void TestHumansOnlyRace(void)
{
	// Without fill, the first car home wins and everyone else loses, as before.
	for (short numHumans = 1; numHumans <= MAX_PLAYERS; numHumans++)
	{
		for (short first = 0; first < numHumans; first++)
		{
			SeatPlayers(numHumans, numHumans);
			short order[MAX_PLAYERS];
			for (short i = 0; i < numHumans; i++)
				order[i] = (first + i) % numHumans;
			Byte results[MAX_PLAYERS];
			CHECK(RunFinishes(order, numHumans, results) == first);
			for (short p = 0; p < numHumans; p++)
				CHECK(results[p] == (p == first ? kRaceResult_Won : kRaceResult_Lost));
		}
	}

	// A network player who left races on as a bot but no longer contests the race: its
	// finish ends nothing, the first human home wins, and the bot gets no result.
	SeatPlayers(4, 4);
	players[2].isComputer = true;
	Byte results[MAX_PLAYERS];
	const short botFirst[] = {2, 0};
	CHECK(RunFinishes(botFirst, 1, results) == -1);
	CHECK(RunFinishes(botFirst, 2, results) == 0);
	CHECK(results[0] == kRaceResult_Won && results[1] == kRaceResult_Lost && results[2] == kRaceResult_None
		&& results[3] == kRaceResult_Lost);
}

static void TestFilledRace(void)
{
	for (short numHumans = 1; numHumans <= MAX_LOCAL_PLAYERS && numHumans <= MAX_PLAYERS; numHumans++)
	{
		for (short winner = 0; winner < numHumans; winner++)
		{
			// Every CPU finishes first, then the humans, winner first.
			SeatPlayers(numHumans, MAX_PLAYERS);
			short order[MAX_PLAYERS];
			int n = 0;
			for (short cpu = numHumans; cpu < MAX_PLAYERS; cpu++)
				order[n++] = cpu;
			order[n++] = winner;
			for (short human = 0; human < numHumans; human++)
				if (human != winner)
					order[n++] = human;
			CHECK(n == MAX_PLAYERS);

			Byte results[MAX_PLAYERS];
			for (int cpusHome = 0; cpusHome < MAX_PLAYERS - numHumans; cpusHome++)
				CHECK(RunFinishes(order, cpusHome + 1, results) == -1);		// CPU finishes never end it
			CHECK(RunFinishes(order, MAX_PLAYERS, results) == winner);
			for (short p = 0; p < MAX_PLAYERS; p++)
			{
				if (p >= numHumans)
					CHECK(results[p] == kRaceResult_None);
				else
					CHECK(results[p] == (p == winner ? kRaceResult_Won : kRaceResult_Lost));
			}
		}
	}

	// A human who wins before any CPU gets home ends the race all the same.
	SeatPlayers(2, MAX_PLAYERS);
	Byte results[MAX_PLAYERS];
	const short humanFirst[] = {1, MAX_PLAYERS - 1, 0};
	CHECK(RunFinishes(humanFirst, 3, results) == 1);
	CHECK(results[0] == kRaceResult_Lost && results[1] == kRaceResult_Won);

	// Humans crossing in the same frame are handled in player order: the first one wins,
	// and nothing the second finish says changes the result.
	SeatPlayers(3, MAX_PLAYERS);
	CHECK(DecideMultiplayerRaceFinish(players, numPlayers, 0, false, results));
	CHECK(!DecideMultiplayerRaceFinish(players, numPlayers, 2, true, results));
	CHECK(results[0] == kRaceResult_Won && results[1] == kRaceResult_Lost && results[2] == kRaceResult_Lost);

	// Once decided, a finish (even the winner's again) does nothing.
	memset(results, 0xAA, sizeof(results));
	CHECK(!DecideMultiplayerRaceFinish(players, numPlayers, 0, true, results));
	CHECK(!DecideMultiplayerRaceFinish(players, numPlayers, 1, true, results));
	for (short p = 0; p < MAX_PLAYERS; p++)
		CHECK(results[p] == 0xAA);

	// Out-of-range finishers are ignored.
	CHECK(!DecideMultiplayerRaceFinish(players, numPlayers, -1, false, results));
	CHECK(!DecideMultiplayerRaceFinish(players, numPlayers, numPlayers, false, results));
}

int main(void)
{
	TestModes();
	TestFillDecision();
	TestPlayerLimits();
	TestPlayerCounts();
	TestDriverLooks();
	TestNetworkFillLooks();
	TestHumansOnlyRace();
	TestFilledRace();
	puts("CPU fill tests passed");
	return EXIT_SUCCESS;
}
