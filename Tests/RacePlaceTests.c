// Production race placing and race completion from Checkpoints.c. Linker section
// GC omits the checkpoint/lap paths that need the rest of the game.
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
Boolean gTrackCompleted;
Boolean gCPUFillThisRace;
float gTrackCompletedCoolDownTimer;

// What PlayerCompletedRace told the players.
typedef struct { short player; Byte mode; short winner; } WinLoseCall;
static WinLoseCall winLoseCalls[MAX_PLAYERS * 4];
static int numWinLoseCalls, numFinalPlaceCalls;

int SaveRaceTime(int playerNum) { (void) playerNum; return -1; }
void ShowFinalPlace(short playerNum, int rankInScoreboard) { (void) playerNum; (void) rankInScoreboard; numFinalPlaceCalls++; }
void ShowWinLose(short playerNum, Byte mode, short winner)
{
	CHECK(numWinLoseCalls < (int) SDL_arraysize(winLoseCalls));
	winLoseCalls[numWinLoseCalls++] = (WinLoseCall){playerNum, mode, winner};
}

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

static void TestFinishingOrderPlaces(void)
{
	// Player 0 is ranked 2nd on the final approach: same lap and checkpoint, but farther
	// from the finish line's midpoint (it's out at the edge of the track).
	StartRace(3, 1);
	gPlayerInfo[1].checkpointNum = gPlayerInfo[0].checkpointNum;
	gPlayerInfo[0].distToNextCheckpoint = 900;
	gPlayerInfo[1].distToNextCheckpoint = 300;
	CalcPlayerPlaces();
	CHECK(gPlayerInfo[0].place == 1 && gPlayerInfo[1].place == 0 && gPlayerInfo[2].place == 2);

	// It crosses the line first, so it's 1st, and the car it beat is 2nd, not tied with it.
	numFinalPlaceCalls = 0;
	PlayerCompletedRace(0);
	CHECK(gPlayerInfo[0].place == 0 && numFinalPlaceCalls == 1);
	CalcPlayerPlaces();
	CHECK(gPlayerInfo[0].place == 0 && gPlayerInfo[1].place == 1 && gPlayerInfo[2].place == 2);

	// Later finishers take the next places in the order they cross, and a repeat call
	// changes nothing.
	PlayerCompletedRace(2);
	PlayerCompletedRace(1);
	PlayerCompletedRace(0);
	CalcPlayerPlaces();
	CHECK(gPlayerInfo[0].place == 0 && gPlayerInfo[2].place == 1 && gPlayerInfo[1].place == 2);
}

static void StartMultiplayerRace(short numPlayers, short numHumans, Boolean cpuFill)
{
	StartRace(numPlayers, numHumans);
	gGameMode = GAME_MODE_MULTIPLAYERRACE;
	gCPUFillThisRace = cpuFill;
	gTrackCompleted = false;
	gTrackCompletedCoolDownTimer = 0;
	numWinLoseCalls = numFinalPlaceCalls = 0;
}

static void TestHumansOnlyRaceCompletion(void)
{
	// The first car home ends the race: it won and every other player lost.
	StartMultiplayerRace(3, 3, false);
	PlayerCompletedRace(1);
	CHECK(gTrackCompleted && gTrackCompletedCoolDownTimer == TRACK_COMPLETE_COOLDOWN_TIME);
	CHECK(numWinLoseCalls == 3 && numFinalPlaceCalls == 0);
	for (int i = 0; i < 3; i++)
	{
		CHECK(winLoseCalls[i].player == i && winLoseCalls[i].winner == 1);
		CHECK(winLoseCalls[i].mode == (i == 1 ? 1 : 2));
	}

	// Later finishes (and a repeated one) tell nobody anything new.
	PlayerCompletedRace(0);
	PlayerCompletedRace(1);
	CHECK(numWinLoseCalls == 3);
}

static void TestFilledRaceCompletion(void)
{
	// Two humans (players 0 and 1) and four CPUs. Player 0 leads the humans.
	StartMultiplayerRace(MAX_PLAYERS, 2, true);
	gPlayerInfo[0].checkpointNum = MAX_PLAYERS + 1;
	gPlayerInfo[1].checkpointNum = 0;

	// A CPU finishing first doesn't end the race, but it now stays ahead of every car
	// still racing, so the humans' places are among all the cars.
	short cpu = MAX_PLAYERS - 1;
	PlayerCompletedRace(cpu);
	CHECK(gPlayerInfo[cpu].raceComplete && !gTrackCompleted && numWinLoseCalls == 0);
	CalcPlayerPlaces();
	CHECK(gPlayerInfo[0].place == 1);
	CHECK(gPlayerInfo[1].place == MAX_PLAYERS - 1);
	CHECK(gWorstHumanPlace == MAX_PLAYERS - 1);

	// The first human home wins; only the humans hear about it.
	PlayerCompletedRace(1);
	CHECK(gTrackCompleted && gTrackCompletedCoolDownTimer == TRACK_COMPLETE_COOLDOWN_TIME);
	CHECK(numWinLoseCalls == 2 && numFinalPlaceCalls == 0);
	CHECK(winLoseCalls[0].player == 0 && winLoseCalls[0].mode == 2 && winLoseCalls[0].winner == 1);
	CHECK(winLoseCalls[1].player == 1 && winLoseCalls[1].mode == 1 && winLoseCalls[1].winner == 1);

	// The already-finished winner, the other human and the CPUs change nothing.
	PlayerCompletedRace(1);
	PlayerCompletedRace(0);
	for (short p = 2; p < MAX_PLAYERS; p++)
		PlayerCompletedRace(p);
	CHECK(numWinLoseCalls == 2);
	for (short p = 0; p < MAX_PLAYERS; p++)
		CHECK(gPlayerInfo[p].raceComplete);
}

int main(void)
{
	TestDemoWorstHumanPlace();
	TestWorstHumanPlace();
	TestFinishingOrderPlaces();
	TestHumansOnlyRaceCompletion();
	TestFilledRaceCompletion();
	puts("Race place tests passed");
	return EXIT_SUCCESS;
}
