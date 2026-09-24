#include "game.h"
#include "race_metrics.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) do { if (!(condition)) { \
	fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); \
	exit(EXIT_FAILURE); } } while (0)

#define NUM_CHECKPOINTS 20
#define DT 0.5f									// no rounding ties when printed with %.1f

static PlayerInfoType players[MAX_PLAYERS + 1];	// one spare slot to check the clamp
static ObjNode cars[MAX_PLAYERS + 1];

static void ResetPlayers(void)
{
	memset(players, 0, sizeof(players));
	memset(cars, 0, sizeof(cars));
	for (int p = 0; p <= MAX_PLAYERS; p++)
	{
		players[p].objNode = &cars[p];
		players[p].isComputer = p != 0;
		players[p].vehicleType = (short) p;
		players[p].place = (short) p;
		players[p].lapNum = -1;
		players[p].checkpointNum = NUM_CHECKPOINTS - 1;			// the grid is behind the finish line
		players[p].coord.x = 10000.0f * (float) p;				// nobody is near anybody
		cars[p].Speed2D = 1000;
	}
	ResetRaceMetrics();
}

static const char* CarLine(int p)
{
	static char line[512];
	FormatRaceMetricsCar(line, sizeof(line), p, &players[p]);
	return line;
}

static void Sample(int numPlayers, int steps)
{
	for (int i = 0; i < steps; i++)
		SampleRaceMetrics(players, numPlayers, NUM_CHECKPOINTS, DT);
}

static void CheckLine(const char* actual, const char* expected)
{
	if (strcmp(actual, expected) != 0)
	{
		fprintf(stderr, "expected: %s\n  actual: %s\n", expected, actual);
		exit(EXIT_FAILURE);
	}
}

static void TestFreshRace(void)
{
	char line[512];

	ResetPlayers();
	FormatRaceMetricsRace(line, sizeof(line), "frame-cap", 6, 2, NUM_CHECKPOINTS);
	CheckLine(line, "METRICS race track=6 cars=2 why=frame-cap racetime=0.0 ckpts=20");
	CheckLine(CarLine(1),
		"METRICS car p=1 cpu=1 veh=1 place=1 lap=-1 ckpt=19 done=0 fin=-1.0 finorder=-1"
		" lap0=-1.0 ck5=-1.0 lap1=-1.0 lap2=-1.0 stuck=0.0 wrong=0.0"
		" bumps=0 hard=0 blasted=0 pickups=0 uses=0 nbr=0.00");
}

static void TestEventHooks(void)
{
	ResetPlayers();
	RaceMetricsCarHit(0, 1, RACE_METRICS_HARD_HIT_SPEED);			// a bump, not a hard hit
	RaceMetricsCarHit(1, 2, RACE_METRICS_HARD_HIT_SPEED + 1);
	RaceMetricsBlast(2, 2);											// your own blast doesn't count
	RaceMetricsBlast(2, 0);
	RaceMetricsBlast(1, -1);										// nobody threw it
	RaceMetricsPickup(0);
	RaceMetricsPickup(0);
	RaceMetricsPOWUse(0);

		/* Out-of-range player numbers are ignored */

	RaceMetricsCarHit(-1, MAX_PLAYERS, 5000);
	RaceMetricsBlast(MAX_PLAYERS, 0);
	RaceMetricsPickup(-1);
	RaceMetricsPOWUse(MAX_PLAYERS);

	CHECK(strstr(CarLine(0), " bumps=1 hard=0 blasted=0 pickups=2 uses=1 "));
	CHECK(strstr(CarLine(1), " bumps=2 hard=1 blasted=1 pickups=0 uses=0 "));
	CHECK(strstr(CarLine(2), " bumps=1 hard=1 blasted=1 pickups=0 uses=0 "));

	ResetRaceMetrics();
	CHECK(strstr(CarLine(1), " bumps=0 hard=0 blasted=0 pickups=0 uses=0 "));
}

static void TestLapsAndTimes(void)
{
	char line[512];

	ResetPlayers();
	cars[0].Speed2D = 100;											// stuck...
	players[0].wrongWay = true;										// ...and facing the wrong way
	Sample(2, 4);													// 2.0 s

	players[0].lapNum = 0;											// both cross the start line
	players[0].checkpointNum = 0;
	players[1].lapNum = 0;
	players[1].checkpointNum = 0;
	Sample(2, 2);													// 3.0 s

	players[1].checkpointNum = 5;
	Sample(2, 2);													// 4.0 s

	players[1].lapNum = 1;
	players[1].checkpointNum = 0;
	Sample(2, 2);													// 5.0 s

	players[1].lapNum = 2;
	Sample(2, 1);													// 5.5 s

	players[1].lapNum = 3;											// finishes
	players[1].raceComplete = true;
	cars[1].Speed2D = 0;											// finished cars are not "stuck"
	Sample(2, 2);													// 6.5 s

	FormatRaceMetricsRace(line, sizeof(line), "player1-finished", 1, 2, NUM_CHECKPOINTS);
	CheckLine(line, "METRICS race track=1 cars=2 why=player1-finished racetime=6.5 ckpts=20");
	CheckLine(CarLine(0),
		"METRICS car p=0 cpu=0 veh=0 place=0 lap=0 ckpt=0 done=0 fin=-1.0 finorder=-1"
		" lap0=2.5 ck5=-1.0 lap1=-1.0 lap2=-1.0 stuck=6.5 wrong=6.5"
		" bumps=0 hard=0 blasted=0 pickups=0 uses=0 nbr=0.00");
	CheckLine(CarLine(1),
		"METRICS car p=1 cpu=1 veh=1 place=1 lap=3 ckpt=0 done=1 fin=6.0 finorder=0"
		" lap0=2.5 ck5=3.5 lap1=4.5 lap2=5.5 stuck=0.0 wrong=0.0"
		" bumps=0 hard=0 blasted=0 pickups=0 uses=0 nbr=0.00");

		/* Finishing order counts up */

	players[0].raceComplete = true;
	Sample(2, 1);
	CHECK(strstr(CarLine(0), " done=1 fin=7.0 finorder=1 "));
	CHECK(strstr(CarLine(1), " fin=6.0 finorder=0 "));				// first finish time sticks
}

// Cars that start behind the last checkpoint pass it on lap 0 before the start line.
static void TestLastCheckpointIsNotCheckpoint5(void)
{
	ResetPlayers();
	players[0].lapNum = 0;
	players[0].checkpointNum = NUM_CHECKPOINTS - 1;
	Sample(1, 1);
	CHECK(strstr(CarLine(0), " ck5=-1.0 "));
	players[0].checkpointNum = 6;									// skipped 5 between two samples
	Sample(1, 1);
	CHECK(strstr(CarLine(0), " ck5=1.0 "));
}

static void TestNeighborsAndMissingCars(void)
{
	ResetPlayers();
	players[1].coord.x = 2499;										// near player 0
	players[2].coord.x = 2501;										// near player 1 only
	players[3].objNode = NULL;										// not sampled, but still counted as a neighbor
	players[3].coord.x = 0;
	cars[3].Speed2D = 0;
	Sample(4, 4);

	CHECK(strstr(CarLine(0), " nbr=2.00"));							// players 1 and 3
	CHECK(strstr(CarLine(1), " nbr=3.00"));
	CHECK(strstr(CarLine(2), " nbr=1.00"));
	CHECK(strstr(CarLine(3), " stuck=0.0 wrong=0.0 ") && strstr(CarLine(3), " nbr=0.00"));
}

// A full field of MAX_PLAYERS cars on one spot; any extra car is ignored.
static void TestFullField(void)
{
	char expected[32];

	ResetPlayers();
	for (int p = 0; p <= MAX_PLAYERS; p++)
		players[p].coord.x = 0;
	Sample(MAX_PLAYERS + 1, 2);
	snprintf(expected, sizeof(expected), " nbr=%d.00", MAX_PLAYERS - 1);
	for (int p = 0; p < MAX_PLAYERS; p++)
		CHECK(strstr(CarLine(p), expected));
}

static int numLogLines;
static char logLines[MAX_PLAYERS + 2][512];

static void CaptureLog(void* userdata, int category, SDL_LogPriority priority, const char* message)
{
	(void) userdata; (void) category; (void) priority;
	CHECK(numLogLines < MAX_PLAYERS + 2);
	SDL_strlcpy(logLines[numLogLines++], message, sizeof(logLines[0]));
}

// One race line, then one line per car in player order; the car count is clamped.
static void TestReport(void)
{
	char expected[512];
	SDL_LogOutputFunction output;
	void* outputData;

	ResetPlayers();
	Sample(MAX_PLAYERS, 1);
	SDL_GetLogOutputFunction(&output, &outputData);
	SDL_SetLogOutputFunction(CaptureLog, NULL);
	ReportRaceMetrics("frame-cap", 9, players, MAX_PLAYERS + 1, NUM_CHECKPOINTS);
	SDL_SetLogOutputFunction(output, outputData);

	CHECK(numLogLines == 1 + MAX_PLAYERS);
	snprintf(expected, sizeof(expected), "METRICS race track=9 cars=%d why=frame-cap racetime=0.5 ckpts=20", MAX_PLAYERS);
	CheckLine(logLines[0], expected);
	for (int p = 0; p < MAX_PLAYERS; p++)
		CheckLine(logLines[1 + p], CarLine(p));
}

int main(void)
{
	TestFreshRace();
	TestEventHooks();
	TestLapsAndTimes();
	TestLastCheckpointIsNotCheckpoint5();
	TestNeighborsAndMissingCars();
	TestFullField();
	TestReport();
	puts("race metrics tests passed");
	return EXIT_SUCCESS;
}
