#include "game.h"
#include "cpu_fill.h"
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
static short RunFinishes(Boolean cpuFill, const short* order, int numFinishes, Byte results[MAX_PLAYERS])
{
	short winner = -1;
	memset(results, 0xFF, MAX_PLAYERS);
	for (int i = 0; i < numFinishes; i++)
	{
		Byte scratch[MAX_PLAYERS];
		Boolean decided = winner >= 0;
		if (DecideMultiplayerRaceFinish(players, numPlayers, order[i], decided, cpuFill, decided ? scratch : results))
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
			CHECK(RunFinishes(false, order, numHumans, results) == first);
			for (short p = 0; p < numHumans; p++)
				CHECK(results[p] == (p == first ? kRaceResult_Won : kRaceResult_Lost));
		}
	}

	// A network player who left races on as a bot, and can still win.
	SeatPlayers(4, 4);
	players[2].isComputer = true;
	Byte results[MAX_PLAYERS];
	const short botFirst[] = {2, 0};
	CHECK(RunFinishes(false, botFirst, 2, results) == 2);
	CHECK(results[2] == kRaceResult_Won && results[0] == kRaceResult_Lost && results[1] == kRaceResult_Lost
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
				CHECK(RunFinishes(true, order, cpusHome + 1, results) == -1);		// CPU finishes never end it
			CHECK(RunFinishes(true, order, MAX_PLAYERS, results) == winner);
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
	CHECK(RunFinishes(true, humanFirst, 3, results) == 1);
	CHECK(results[0] == kRaceResult_Lost && results[1] == kRaceResult_Won);

	// Humans crossing in the same frame are handled in player order: the first one wins,
	// and nothing the second finish says changes the result.
	SeatPlayers(3, MAX_PLAYERS);
	CHECK(DecideMultiplayerRaceFinish(players, numPlayers, 0, false, true, results));
	CHECK(!DecideMultiplayerRaceFinish(players, numPlayers, 2, true, true, results));
	CHECK(results[0] == kRaceResult_Won && results[1] == kRaceResult_Lost && results[2] == kRaceResult_Lost);

	// Once decided, a finish (even the winner's again) does nothing.
	memset(results, 0xAA, sizeof(results));
	CHECK(!DecideMultiplayerRaceFinish(players, numPlayers, 0, true, true, results));
	CHECK(!DecideMultiplayerRaceFinish(players, numPlayers, 1, true, false, results));
	for (short p = 0; p < MAX_PLAYERS; p++)
		CHECK(results[p] == 0xAA);

	// Out-of-range finishers are ignored.
	CHECK(!DecideMultiplayerRaceFinish(players, numPlayers, -1, false, true, results));
	CHECK(!DecideMultiplayerRaceFinish(players, numPlayers, numPlayers, false, false, results));
}

int main(void)
{
	TestModes();
	TestHumansOnlyRace();
	TestFilledRace();
	puts("CPU fill tests passed");
	return EXIT_SUCCESS;
}
