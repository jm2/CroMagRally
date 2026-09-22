// Production race placing from Checkpoints.c. Linker section GC omits the
// checkpoint/lap paths that need the rest of the game.
#include "game.h"
#include <stdio.h>
#include <stdlib.h>

#define CHECK(condition) do { if (!(condition)) { \
	fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); \
	exit(EXIT_FAILURE); } } while (0)

PlayerInfoType gPlayerInfo[MAX_PLAYERS];
short gNumTotalPlayers;
int gGameMode;
Boolean gIsSelfRunningDemo;

// Player p is p checkpoints behind the leader, so places run 0..numPlayers-1.
static void StartRace(short numPlayers, short numHumans)
{
	memset(gPlayerInfo, 0, sizeof(gPlayerInfo));
	gGameMode = GAME_MODE_PRACTICE;
	gNumTotalPlayers = numPlayers;
	for (short p = 0; p < numPlayers; p++)
	{
		gPlayerInfo[p].isComputer = p >= numHumans;
		gPlayerInfo[p].checkpointNum = numPlayers - p;
	}
}

static void TestDemoWorstHumanPlace(void)
{
	// The self-running demo has no real humans, so "worst human" is last place:
	// every CPU car attacks the others and none gets the catch-up boost.
	gIsSelfRunningDemo = true;
	for (short numPlayers = 1; numPlayers <= MAX_PLAYERS; numPlayers++)
	{
		StartRace(numPlayers, 1);
		CalcPlayerPlaces();
		for (short p = 0; p < numPlayers; p++)
			CHECK(gPlayerInfo[p].place == p);
		CHECK(gWorstHumanPlace == numPlayers - 1);
	}
	gIsSelfRunningDemo = false;
}

static void TestWorstHumanPlace(void)
{
	// Outside the demo, it is the place of the rearmost human.
	StartRace(MAX_PLAYERS, 1);
	CalcPlayerPlaces();
	CHECK(gWorstHumanPlace == 0);
	gPlayerInfo[0].checkpointNum = 0;
	CalcPlayerPlaces();
	CHECK(gWorstHumanPlace == MAX_PLAYERS - 1);
}

int main(void)
{
	TestDemoWorstHumanPlace();
	TestWorstHumanPlace();
	puts("Race place tests passed");
	return EXIT_SUCCESS;
}
