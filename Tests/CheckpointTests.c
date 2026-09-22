#include "game.h"
#include <stdio.h>
#include <stdlib.h>

#define CHECK(condition) do { if (!(condition)) { \
	fprintf(stderr, "%s:%d: %s (numCheckpoints=%ld)\n", __FILE__, __LINE__, #condition, gTestCheckpoints); \
	exit(EXIT_FAILURE); } } while (0)

static long gTestCheckpoints;

typedef struct
{
	long	numCheckpoints;
	short	lapNum;
	short	checkpointNum;
	Boolean	tagged[MAX_CHECKPOINTS];
	Boolean	raceComplete;
} Racer;

// Same state as InitPlayersAtStartOfLevel.
static Racer StartRace(long numCheckpoints)
{
	Racer r = {.numCheckpoints = numCheckpoints, .lapNum = -1, .checkpointNum = (short)(numCheckpoints - 1)};
	for (int i = 0; i < MAX_CHECKPOINTS; i++)
		r.tagged[i] = true;
	gTestCheckpoints = numCheckpoints;
	return r;
}

// Cross checkpoint c like UpdatePlayerCheckpoints, then apply NextLap's lap state.
// Returns true if the crossing counted a lap.
static Boolean Cross(Racer* r, short c)
{
	CHECK(!r->raceComplete);								// UpdatePlayerCheckpoints stops after the race
	CHECK(0 <= c && c < r->numCheckpoints);
	Boolean lap = CrossCheckpoint(&r->checkpointNum, r->tagged, r->numCheckpoints, c);
	CHECK(0 <= r->checkpointNum && r->checkpointNum < r->numCheckpoints);
	if (lap)
	{
		r->lapNum++;
		r->raceComplete = r->lapNum >= LAPS_PER_RACE;
		for (int i = 0; i < r->numCheckpoints; i++)
			r->tagged[i] = false;
	}
	return lap;
}

static int CountTags(const Racer* r)
{
	int count = 0;
	for (int i = 0; i < r->numCheckpoints; i++)
		count += r->tagged[i] ? 1 : 0;
	return count;
}

// Drive forward over checkpoints 1..N-1, then the finish line.
static Boolean DriveLap(Racer* r)
{
	for (short c = 1; c < r->numCheckpoints; c++)
		CHECK(!Cross(r, c));
	return Cross(r, 0);
}

// Finish the race from just past the finish line on lap 0.
static void DriveRestOfRace(Racer* r)
{
	CHECK(r->lapNum == 0 && r->checkpointNum == 0);
	for (int lap = 1; lap <= LAPS_PER_RACE; lap++)
	{
		CHECK(!r->raceComplete);
		CHECK(DriveLap(r));
		CHECK(r->lapNum == lap);
	}
	CHECK(r->raceComplete);
}

// Every shipped grid sits between checkpoint N-1 and the finish line, so the first
// crossing is the finish line. All tags start set, so it begins lap 0.
static void TestGridStart(long n)
{
	Racer r = StartRace(n);
	CHECK(Cross(&r, 0));
	CHECK(r.lapNum == 0 && r.checkpointNum == 0 && CountTags(&r) == 0);
	DriveRestOfRace(&r);
}

// Reversing over checkpoint N-1 before the start and driving back still starts lap 0.
static void TestReverseAtGrid(long n)
{
	Racer r = StartRace(n);
	CHECK(!Cross(&r, (short)(n - 1)));
	CHECK(!Cross(&r, (short)(n - 1)));
	CHECK(r.lapNum == -1 && r.checkpointNum == n - 1);
	CHECK(Cross(&r, 0));
	DriveRestOfRace(&r);
}

// A slot two or more checkpoints back reads its first crossing as going backward,
// then re-tags N-1 and still starts lap 0 at the finish line.
static void TestStartBehindEarlierCheckpoint(long n)
{
	for (short first = 1; first < n - 1; first++)
	{
		Racer r = StartRace(n);
		for (short c = first; c < n; c++)
			CHECK(!Cross(&r, c));
		CHECK(r.checkpointNum == n - 1);
		CHECK(Cross(&r, 0));
		CHECK(r.lapNum == 0);
		DriveRestOfRace(&r);
	}
}

// Backing over the finish line clears the tags without taking the lap back, and
// driving forward over it again doesn't count that lap twice.
static void TestBackOverFinishLine(long n)
{
	Racer r = StartRace(n);
	CHECK(Cross(&r, 0));
	CHECK(DriveLap(&r));
	CHECK(r.lapNum == 1);

	CHECK(!Cross(&r, 0));									// back over the finish line
	CHECK(r.lapNum == 1 && r.checkpointNum == 0 && CountTags(&r) == 0);
	CHECK(!Cross(&r, 0));									// and forward again
	CHECK(r.lapNum == 1 && r.checkpointNum == 0 && CountTags(&r) == 0);

	CHECK(!Cross(&r, 0));									// back over it, and back over N-1 too
	CHECK(!Cross(&r, (short)(n - 1)));
	CHECK(r.checkpointNum == n - 1 && CountTags(&r) == 1);
	CHECK(!Cross(&r, (short)(n - 1)));						// then forward over both
	CHECK(r.checkpointNum == n - 2 && CountTags(&r) == 0);
	CHECK(!Cross(&r, 0));
	CHECK(r.lapNum == 1);

	CHECK(DriveLap(&r));									// only a real lap counts again
	CHECK(r.lapNum == 2);
}

// Backing over a middle checkpoint untags it and moves back one; driving forward
// over it again re-tags it.
static void TestBackOverMiddleCheckpoint(long n)
{
	Racer r = StartRace(n);
	CHECK(Cross(&r, 0));
	CHECK(!Cross(&r, 1));
	CHECK(!Cross(&r, 2));
	CHECK(!Cross(&r, 3));
	CHECK(r.checkpointNum == 3 && r.tagged[1] && r.tagged[2] && r.tagged[3] && CountTags(&r) == 3);

	CHECK(!Cross(&r, 3));
	CHECK(r.checkpointNum == 2 && !r.tagged[3] && CountTags(&r) == 2);
	CHECK(!Cross(&r, 2));
	CHECK(r.checkpointNum == 1 && !r.tagged[2] && CountTags(&r) == 1);

	CHECK(!Cross(&r, 2));
	CHECK(!Cross(&r, 3));
	CHECK(r.checkpointNum == 3 && CountTags(&r) == 3);

	// Past N-1 after the first lap, backing over N-1 untags it as well.
	for (short c = 4; c < n; c++)
		CHECK(!Cross(&r, c));
	CHECK(r.checkpointNum == n - 1 && r.tagged[n - 1]);
	CHECK(!Cross(&r, (short)(n - 1)));
	CHECK(r.checkpointNum == n - 2 && !r.tagged[n - 1]);
	CHECK(!Cross(&r, (short)(n - 1)));
	CHECK(Cross(&r, 0));
	CHECK(r.lapNum == 1);
}

// The finish line only counts from checkpoint N-1 with more than half the
// checkpoints tagged.
static void TestSkippedCheckpoints(long n)
{
	// Tag 1..k, then jump to N-1: k + 1 tags.
	for (short k = 0; k < n - 1; k++)
	{
		Racer r = StartRace(n);
		CHECK(Cross(&r, 0));
		for (short c = 1; c <= k; c++)
			CHECK(!Cross(&r, c));
		CHECK(!Cross(&r, (short)(n - 1)));
		CHECK(CountTags(&r) == k + 1);
		CHECK(Cross(&r, 0) == (k + 1 > n / 2));
		CHECK(r.checkpointNum == 0 && CountTags(&r) == 0);
	}

	// Cutting over the finish line from anywhere but N-1 never counts.
	for (short last = 1; last < n - 1; last++)
	{
		Racer r = StartRace(n);
		CHECK(Cross(&r, 0));
		for (short c = 1; c <= last; c++)
			CHECK(!Cross(&r, c));
		CHECK(!Cross(&r, 0));
		CHECK(r.lapNum == 0 && r.checkpointNum == 0 && CountTags(&r) == 0);
	}

	// Nor does a lap driven the wrong way.
	Racer r = StartRace(n);
	CHECK(Cross(&r, 0));
	for (short c = 0; c < n; c++)
		CHECK(!Cross(&r, (short)((n - c) % n)));
	CHECK(!Cross(&r, 0));
	CHECK(r.lapNum == 0);
}

int main(void)
{
	for (long n = 4; n <= MAX_CHECKPOINTS; n++)
	{
		TestGridStart(n);
		TestReverseAtGrid(n);
		TestStartBehindEarlierCheckpoint(n);
		TestBackOverFinishLine(n);
		TestBackOverMiddleCheckpoint(n);
		TestSkippedCheckpoints(n);
	}
	puts("Checkpoint lap tests passed");
	return 0;
}
