// Start slots for players without an authored MyStartCoord item (StartSlots.c): the generated
// table for the shipped maps, the procedural rule for other maps, and keeping humans at the back
// of a race grid. Tests/StartSlotTableTests.py checks the table against the map data itself.

#include "globals.h"
#include "startslots.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define CHECK(condition) do { if (!(condition)) { \
	fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); \
	exit(EXIT_FAILURE); } } while (0)

#define CHECK_ENTRY(condition, entry) do { if (!(condition)) { \
	fprintf(stderr, "%s:%d: %s (%s set %d)\n", __FILE__, __LINE__, #condition, (entry)->map, (int) (entry)->set); \
	exit(EXIT_FAILURE); } } while (0)

enum
{
	AUTHORED		= START_SLOT_TABLE_AUTHORED,
	TABLE_SLOTS		= START_SLOT_TABLE_AUTHORED + START_SLOT_TABLE_EXTRA,
	MAX_TEST_SLOTS	= 16,
	MIN_SPACING		= 900,						// tools/gen_start_slots.py MIN_SPACING
	RACE_BEHIND		= 900,						// ...RACE_BEHIND
	ROW_TOL			= 400,						// ...ROW_TOL
};

static int64_t Dist2(int x0, int z0, int x1, int z1)
{
	const int64_t dx = (int64_t) x0 - x1, dz = (int64_t) z0 - z1;
	return dx * dx + dz * dz;
}

// How far along the grid's forward vector (-sin rotY, -cos rotY) a slot is.
static double Depth(int x, int z, int rot16)
{
	const double rot = 2.0 * 3.14159265358979323846 * rot16 / 16.0;
	return x * -sin(rot) + z * -cos(rot);
}

static bool SamePose(StartSlotPose pose, StartSlot slot)
{
	return pose.x == slot.x && pose.z == slot.z && pose.rotY == StartSlot_RotY(slot.rot16);
}

static bool PosesEqual(StartSlotPose a, StartSlotPose b)
{
	return a.x == b.x && a.z == b.z && a.rotY == b.rotY;
}

// Players 0-5 authored as in the table entry, the rest missing.
static void LoadAuthored(const StartSlotTableEntry* entry, StartSlot items[], bool authored[], int numSlots)
{
	for (int p = 0; p < numSlots; p++)
	{
		authored[p] = p < AUTHORED;
		items[p] = p < AUTHORED ? entry->authored[p] : (StartSlot) { 0, 0, 0 };
	}
}

static const StartSlotTableEntry* Fill(const StartSlotTableEntry* entry, int numSlots, StartSlotPose poses[])
{
	StartSlot items[MAX_TEST_SLOTS];
	bool authored[MAX_TEST_SLOTS];
	LoadAuthored(entry, items, authored, numSlots);
	return StartSlots_Fill(entry->set, entry->mapUnitWidth, entry->mapUnitDepth, items, authored, numSlots, poses);
}

static void CheckDistinctAndSpaced(const StartSlotTableEntry* entry, const StartSlotPose poses[], int numSlots)
{
	for (int p = 0; p < numSlots; p++)
	{
		for (int q = p + 1; q < numSlots; q++)
		{
			const int64_t d2 = Dist2(poses[p].x, poses[p].z, poses[q].x, poses[q].z);
			CHECK_ENTRY(d2 > 0, entry);
			if (q >= AUTHORED)										// (authored pairs are the map's own: Desert has one at 856)
				CHECK_ENTRY(d2 >= (int64_t) MIN_SPACING * MIN_SPACING, entry);
		}
	}
}


/*************** EVERY TABLE ENTRY ****************/

static void TestTableEntries(void)
{
	int perSet[3] = {0, 0, 0};

	CHECK(kNumStartSlotTableEntries == 9 + 8 + 8);				// 9 race grids; 8 arenas x (battle, CTF)

	for (int e = 0; e < kNumStartSlotTableEntries; e++)
	{
		const StartSlotTableEntry* entry = &kStartSlotTable[e];

		CHECK_ENTRY(entry->set >= START_SLOT_SET_RACE && entry->set <= START_SLOT_SET_CTF, entry);
		perSet[entry->set]++;
		CHECK_ENTRY(entry->mapUnitWidth > 0 && entry->mapUnitDepth > 0, entry);

		for (int f = 0; f < e; f++)									// the key picks out exactly one entry
		{
			const StartSlotTableEntry* other = &kStartSlotTable[f];
			bool same = other->set == entry->set && other->mapUnitWidth == entry->mapUnitWidth
					&& other->mapUnitDepth == entry->mapUnitDepth;
			for (int p = 0; same && p < AUTHORED; p++)
				same = other->authored[p].x == entry->authored[p].x && other->authored[p].z == entry->authored[p].z
					&& other->authored[p].rot16 == entry->authored[p].rot16;
			CHECK_ENTRY(!same, entry);
		}

		StartSlotPose poses[TABLE_SLOTS];
		CHECK_ENTRY(Fill(entry, TABLE_SLOTS, poses) == entry, entry);

		for (int p = 0; p < TABLE_SLOTS; p++)
		{
			const StartSlot slot = p < AUTHORED ? entry->authored[p] : entry->extra[p - AUTHORED];
			CHECK_ENTRY(SamePose(poses[p], slot), entry);
			CHECK_ENTRY(slot.rot16 >= 0 && slot.rot16 < 16, entry);
			CHECK_ENTRY(poses[p].x >= 0 && poses[p].x < entry->mapUnitWidth, entry);
			CHECK_ENTRY(poses[p].z >= 0 && poses[p].z < entry->mapUnitDepth, entry);
		}
		CheckDistinctAndSpaced(entry, poses, TABLE_SLOTS);

				/* CTF: EACH NEW SLOT IS NEARER ITS OWN TEAM THAN THE OTHER TEAM */

		if (entry->set == START_SLOT_SET_CTF)
		{
			double tx[2] = {0, 0}, tz[2] = {0, 0};
			for (int p = 0; p < AUTHORED; p++)
			{
				tx[p & 1] += entry->authored[p].x / 3.0;
				tz[p & 1] += entry->authored[p].z / 3.0;
			}
			for (int p = AUTHORED; p < TABLE_SLOTS; p++)
			{
				const int team = p & 1;
				const double own = (poses[p].x - tx[team]) * (poses[p].x - tx[team]) + (poses[p].z - tz[team]) * (poses[p].z - tz[team]);
				const double foe = (poses[p].x - tx[!team]) * (poses[p].x - tx[!team]) + (poses[p].z - tz[!team]) * (poses[p].z - tz[!team]);
				CHECK_ENTRY(own < foe, entry);
			}
		}

				/* RACE: THE NEW SLOTS FACE LIKE THE GRID, BEHIND ALL OF IT */

		if (entry->set == START_SLOT_SET_RACE)
		{
			double rear = 1e30;
			for (int p = 0; p < AUTHORED; p++)
				rear = fmin(rear, Depth(entry->authored[p].x, entry->authored[p].z, entry->authored[0].rot16));
			for (int p = 0; p < START_SLOT_TABLE_EXTRA; p++)
			{
				CHECK_ENTRY(entry->extra[p].rot16 == entry->authored[p].rot16, entry);
				CHECK_ENTRY(rear - Depth(entry->extra[p].x, entry->extra[p].z, entry->authored[0].rot16) >= (double) RACE_BEHIND - 1, entry);
			}
		}
	}

	CHECK(perSet[START_SLOT_SET_RACE] == 9);
	CHECK(perSet[START_SLOT_SET_BATTLE] == 8);
	CHECK(perSet[START_SLOT_SET_CTF] == 8);
}


/*************** ANY NUMBER OF PLAYERS ****************/
//
// MAX_PLAYERS is 6 today: every shipped slot is authored and nothing changes.
//

static void TestPlayerCounts(void)
{
	for (int e = 0; e < kNumStartSlotTableEntries; e++)
	{
		const StartSlotTableEntry* entry = &kStartSlotTable[e];
		StartSlotPose poses[MAX_TEST_SLOTS], before[MAX_TEST_SLOTS];

				/* 6 PLAYERS: AUTHORED ONLY, AND NO SWAPS */

		CHECK_ENTRY(Fill(entry, AUTHORED, poses) == NULL, entry);
		for (int p = 0; p < AUTHORED; p++)
		{
			CHECK_ENTRY(SamePose(poses[p], entry->authored[p]), entry);
			before[p] = poses[p];
		}
		const bool oneHuman[MAX_TEST_SLOTS] = {false, true, true, true, true, true, true, true, true, true, true, true, true, true, true, true};
		StartSlots_KeepHumansAtBack(poses, oneHuman, AUTHORED, AUTHORED);
		for (int p = 0; p < AUTHORED; p++)
			CHECK_ENTRY(PosesEqual(poses[p], before[p]), entry);

				/* 8 PLAYERS: THE TABLE'S FIRST TWO */

		CHECK_ENTRY(Fill(entry, 8, poses) == entry, entry);
		CHECK_ENTRY(SamePose(poses[6], entry->extra[0]) && SamePose(poses[7], entry->extra[1]), entry);

				/* 16 PLAYERS: MORE THAN THE TABLE HAS, SO THE RULE FILLS THEM ALL */

		CHECK_ENTRY(Fill(entry, MAX_TEST_SLOTS, poses) == NULL, entry);
		for (int p = 0; p < AUTHORED; p++)
			CHECK_ENTRY(SamePose(poses[p], entry->authored[p]), entry);
		for (int p = AUTHORED; p < MAX_TEST_SLOTS; p++)
		{
			CHECK_ENTRY(poses[p].x >= 0 && poses[p].x < entry->mapUnitWidth, entry);
			CHECK_ENTRY(poses[p].z >= 0 && poses[p].z < entry->mapUnitDepth, entry);
			for (int q = 0; q < p; q++)
				CHECK_ENTRY(poses[q].x != poses[p].x || poses[q].z != poses[p].z, entry);
		}
	}
}


/*************** HUMANS START AT THE BACK OF A RACE GRID ****************/

static void TestHumansAtTheBack(void)
{
	for (int e = 0; e < kNumStartSlotTableEntries; e++)
	{
		const StartSlotTableEntry* entry = &kStartSlotTable[e];
		if (entry->set != START_SLOT_SET_RACE)
			continue;

		const int rot16 = entry->authored[0].rot16;
		double rear = 1e30;
		for (int p = 0; p < AUTHORED; p++)
			rear = fmin(rear, Depth(entry->authored[p].x, entry->authored[p].z, rot16));
		const double p0Gap = Depth(entry->authored[0].x, entry->authored[0].z, rot16) - rear;

		for (int humans = 1; humans <= 4; humans++)
		{
			StartSlotPose poses[TABLE_SLOTS];
			bool isComputer[TABLE_SLOTS];
			for (int p = 0; p < TABLE_SLOTS; p++)
				isComputer[p] = p >= humans;

			CHECK_ENTRY(Fill(entry, TABLE_SLOTS, poses) == entry, entry);
			StartSlots_KeepHumansAtBack(poses, isComputer, TABLE_SLOTS, AUTHORED);

			for (int p = 0; p < TABLE_SLOTS; p++)
			{
				if (p < humans)											// humans take the new wave's matching slots...
					CHECK_ENTRY(SamePose(poses[p], entry->extra[p]), entry);
				else if (p >= AUTHORED && p < AUTHORED + humans)		// ...and those CPUs the humans' authored slots
					CHECK_ENTRY(SamePose(poses[p], entry->authored[p - AUTHORED]), entry);
				else													// everyone else stays put
					CHECK_ENTRY(SamePose(poses[p], p < AUTHORED ? entry->authored[p] : entry->extra[p - AUTHORED]), entry);
			}

					/* EVERY HUMAN IS BEHIND EVERY CPU ON THE AUTHORED GRID */

			for (int h = 0; h < humans; h++)
			{
				const double depth = Depth(poses[h].x, poses[h].z, rot16);
				for (int p = humans; p < AUTHORED + humans; p++)		// the CPUs on authored slots
					CHECK_ENTRY(Depth(poses[p].x, poses[p].z, rot16) - depth >= (double) RACE_BEHIND - 1, entry);
			}

					/* A LONE HUMAN STARTS IN THE REAR ROW, AS ON THE AUTHORED GRID */

			if (humans == 1)
			{
				double back = 1e30;
				for (int p = 0; p < TABLE_SLOTS; p++)
					back = fmin(back, Depth(poses[p].x, poses[p].z, rot16));
				CHECK_ENTRY(Depth(poses[0].x, poses[0].z, rot16) - back <= p0Gap + (double) ROW_TOL + 1, entry);
			}
		}

				/* 7 CARS: ONLY SLOT 6 EXISTS BEHIND THE GRID, SO ONLY PLAYER 0 MOVES THERE */

		StartSlotPose poses[TABLE_SLOTS];
		bool isComputer[TABLE_SLOTS];
		for (int p = 0; p < TABLE_SLOTS; p++)
			isComputer[p] = p >= 2;
		Fill(entry, TABLE_SLOTS, poses);
		StartSlots_KeepHumansAtBack(poses, isComputer, 7, AUTHORED);
		CHECK_ENTRY(SamePose(poses[0], entry->extra[0]) && SamePose(poses[6], entry->authored[0]), entry);
		CHECK_ENTRY(SamePose(poses[1], entry->authored[1]), entry);
	}
}


/*************** THE PROCEDURAL RULE (MAPS THE TABLE DOESN'T KNOW) ****************/

// A 2 x 3 grid facing -z (rot16 0): lanes 10000 / 11200, rows 20000 (front) to 22500 (rear).
static const StartSlot kGrid[AUTHORED] =
{
	{10000, 22500, 0}, {11200, 22500, 0}, {10000, 21250, 0}, {11200, 21250, 0}, {10000, 20000, 0}, {11200, 20000, 0},
};

static void TestRuleRaceGrid(void)
{
	StartSlot items[MAX_TEST_SLOTS] = {{0, 0, 0}};
	bool authored[MAX_TEST_SLOTS] = {false};
	StartSlotPose poses[MAX_TEST_SLOTS];

	for (int p = 0; p < AUTHORED; p++)
	{
		items[p] = kGrid[p];
		authored[p] = true;
	}

			/* ONE WAVE PER 6 PLAYERS, EACH GRID DEPTH + 1300 FURTHER BACK */

	CHECK(StartSlots_Fill(START_SLOT_SET_RACE, 64000, 64000, items, authored, MAX_TEST_SLOTS, poses) == NULL);
	for (int p = AUTHORED; p < MAX_TEST_SLOTS; p++)
	{
		const StartSlot src = kGrid[p % AUTHORED];
		CHECK(poses[p].x == src.x && poses[p].z == src.z + 3800 * (p / AUTHORED));
		CHECK(poses[p].rotY == StartSlot_RotY(0));
	}

			/* ANOTHER HEADING: FACING -x (rot16 4), SO THE WAVES GO +x */

	for (int p = 0; p < AUTHORED; p++)
		items[p] = (StartSlot) { kGrid[p].z, kGrid[p].x, 4 };
	CHECK(StartSlots_Fill(START_SLOT_SET_RACE, 64000, 64000, items, authored, TABLE_SLOTS, poses) == NULL);
	for (int p = AUTHORED; p < TABLE_SLOTS; p++)
		CHECK(poses[p].x == kGrid[p - AUTHORED].z + 3800 && poses[p].z == kGrid[p - AUTHORED].x);

			/* KEPT ON THE PLAYFIELD */

	CHECK(StartSlots_Fill(START_SLOT_SET_RACE, 23000, 64000, items, authored, TABLE_SLOTS, poses) == NULL);
	for (int p = AUTHORED; p < TABLE_SLOTS; p++)
		CHECK(poses[p].x == 23000 - 1);

			/* HUMANS TO THE BACK ON AN UNKNOWN MAP TOO */

	for (int p = 0; p < AUTHORED; p++)
		items[p] = kGrid[p];
	bool isComputer[TABLE_SLOTS];
	for (int p = 0; p < TABLE_SLOTS; p++)
		isComputer[p] = p != 0;
	StartSlots_Fill(START_SLOT_SET_RACE, 64000, 64000, items, authored, TABLE_SLOTS, poses);
	StartSlots_KeepHumansAtBack(poses, isComputer, TABLE_SLOTS, StartSlots_CountAuthored(authored, TABLE_SLOTS));
	CHECK(poses[0].x == 10000 && poses[0].z == 22500 + 3800);
	CHECK(poses[6].x == 10000 && poses[6].z == 22500);

			/* A MAP WITH FEWER THAN 6 SLOTS NO LONGER STARTS PLAYERS IN THE CORNER */

	authored[4] = authored[5] = false;
	CHECK(StartSlots_Fill(START_SLOT_SET_RACE, 64000, 64000, items, authored, AUTHORED, poses) == NULL);
	CHECK(poses[4].x == 10000 && poses[4].z == 22500 + 2550);		// grid depth 1250 + 1300 behind player 0
	CHECK(poses[5].x == 11200 && poses[5].z == 22500 + 2550);

			/* NO AUTHORED SLOTS AT ALL: NOTHING TO DERIVE FROM */

	for (int p = 0; p < AUTHORED; p++)
		authored[p] = false;
	CHECK(StartSlots_Fill(START_SLOT_SET_RACE, 64000, 64000, items, authored, AUTHORED, poses) == NULL);
	for (int p = 0; p < AUTHORED; p++)
		CHECK(poses[p].x == 0 && poses[p].z == 0 && poses[p].rotY == 0);
}

static void TestRuleArena(void)
{
	StartSlot items[TABLE_SLOTS] = {{0, 0, 0}};
	bool authored[TABLE_SLOTS] = {false};
	StartSlotPose poses[TABLE_SLOTS];

			/* BATTLE: A WIDER RING, HALF A SLOT ROUND */

	for (int p = 0; p < AUTHORED; p++)
	{
		const double a = 2.0 * 3.14159265358979323846 * p / (double) AUTHORED;
		items[p] = (StartSlot) { 30000 + (int) lround(1500 * cos(a)), 30000 + (int) lround(1500 * sin(a)), (p * 16 / AUTHORED) & 15 };
		authored[p] = true;
	}
	CHECK(StartSlots_Fill(START_SLOT_SET_BATTLE, 64000, 64000, items, authored, TABLE_SLOTS, poses) == NULL);
	for (int p = AUTHORED; p < TABLE_SLOTS; p++)
	{
		const double r = sqrt((double) Dist2(poses[p].x, poses[p].z, 30000, 30000));
		CHECK(fabs(r - 2800) < 3);
		CHECK(fabsf(poses[p].rotY - (StartSlot_RotY(items[p - AUTHORED].rot16) + PI2 / 12)) < 1e-5f);
		for (int q = 0; q < p; q++)
			CHECK(Dist2(poses[p].x, poses[p].z, poses[q].x, poses[q].z) >= (int64_t) MIN_SPACING * MIN_SPACING);
	}

			/* ANY RING SIZE: THE TURN (COMPUTED WITHOUT LIBM) MATCHES cos/sin */

	for (int n = 2; n <= 8; n++)
	{
		StartSlot ring[MAX_TEST_SLOTS] = {{0, 0, 0}};
		bool ringAuthored[MAX_TEST_SLOTS] = {false};
		StartSlotPose ringPoses[MAX_TEST_SLOTS];

		for (int p = 0; p < n; p++)
		{
			const double a = 2.0 * 3.14159265358979323846 * p / n;
			ring[p] = (StartSlot) { 30000 + (int) lround(1500 * cos(a)), 30000 + (int) lround(1500 * sin(a)), (p * 16 / n) & 15 };
			ringAuthored[p] = true;
		}
		CHECK(StartSlots_Fill(START_SLOT_SET_BATTLE, 64000, 64000, ring, ringAuthored, 2 * n, ringPoses) == NULL);

		double cx = 0, cz = 0;
		for (int p = 0; p < n; p++)
		{
			cx += ring[p].x / (double) n;
			cz += ring[p].z / (double) n;
		}
		const float theta = PI2 / (float) (2 * n);
		for (int p = n; p < 2 * n; p++)
		{
			const double dx = ring[p - n].x - cx, dz = ring[p - n].z - cz;
			const double k = (sqrt(dx * dx + dz * dz) + 1300) / sqrt(dx * dx + dz * dz);
			CHECK(fabs(ringPoses[p].x - (cx + k * (dx * cos(theta) + dz * sin(theta)))) <= 1.0);
			CHECK(fabs(ringPoses[p].z - (cz + k * (dz * cos(theta) - dx * sin(theta)))) <= 1.0);
			CHECK(ringPoses[p].rotY == StartSlot_RotY(ring[p - n].rot16) + theta);
		}
	}

			/* CTF: A COLUMN BESIDE EACH TEAM'S LINE, TOWARDS THE OTHER TEAM */

	for (int p = 0; p < AUTHORED; p++)
		items[p] = (StartSlot) { (p & 1) ? 40000 : 20000, 29000 + 1000 * (p / 2), (p & 1) ? 4 : 12 };
	CHECK(StartSlots_Fill(START_SLOT_SET_CTF, 64000, 64000, items, authored, TABLE_SLOTS, poses) == NULL);
	for (int p = AUTHORED; p < TABLE_SLOTS; p++)
	{
		CHECK(poses[p].x == ((p & 1) ? 39000 : 21000));
		CHECK(poses[p].z == items[p - AUTHORED].z);
		CHECK(poses[p].rotY == StartSlot_RotY(items[p - AUTHORED].rot16));
	}
}


/*************** THE TABLE NEEDS THE MAP EXACTLY AS SHIPPED ****************/

static void TestTableNeedsExactMatch(void)
{
	const StartSlotTableEntry* entry = &kStartSlotTable[0];
	StartSlot items[TABLE_SLOTS];
	bool authored[TABLE_SLOTS];
	StartSlotPose poses[TABLE_SLOTS], rule[TABLE_SLOTS];

	LoadAuthored(entry, items, authored, TABLE_SLOTS);
	CHECK(StartSlots_Fill(entry->set, entry->mapUnitWidth, entry->mapUnitDepth, items, authored, TABLE_SLOTS, poses) == entry);

			/* A MOVED SLOT: THE PROCEDURAL RULE */

	items[3].x += 50;
	CHECK(StartSlots_Fill(entry->set, entry->mapUnitWidth, entry->mapUnitDepth, items, authored, TABLE_SLOTS, rule) == NULL);
	CHECK(SamePose(rule[3], items[3]));
	for (int p = AUTHORED; p < TABLE_SLOTS; p++)
	{
		CHECK(rule[p].rotY == StartSlot_RotY(items[p - AUTHORED].rot16));
		CHECK(Depth(rule[p].x, rule[p].z, items[0].rot16) < Depth(items[p - AUTHORED].x, items[p - AUTHORED].z, items[0].rot16) - 1300);
		for (int q = 0; q < p; q++)
			CHECK(Dist2(rule[p].x, rule[p].z, rule[q].x, rule[q].z) > 0);
	}
	items[3].x -= 50;

			/* A TURNED SLOT, ANOTHER MAP SIZE OR ANOTHER SET */

	items[5].rot16 ^= 1;
	CHECK(StartSlots_Fill(entry->set, entry->mapUnitWidth, entry->mapUnitDepth, items, authored, TABLE_SLOTS, poses) == NULL);
	items[5].rot16 ^= 1;
	CHECK(StartSlots_Fill(entry->set, entry->mapUnitWidth + 6400, entry->mapUnitDepth, items, authored, TABLE_SLOTS, poses) == NULL);
	CHECK(StartSlots_Fill(START_SLOT_SET_BATTLE, entry->mapUnitWidth, entry->mapUnitDepth, items, authored, TABLE_SLOTS, poses) == NULL);

			/* A MAP THAT AUTHORS A 7TH SLOT KEEPS IT, AND GETS THE RULE FOR THE REST */

	items[6] = (StartSlot) { 1000, 2000, 3 };
	authored[6] = true;
	CHECK(StartSlots_Fill(entry->set, entry->mapUnitWidth, entry->mapUnitDepth, items, authored, TABLE_SLOTS, poses) == NULL);
	CHECK(SamePose(poses[6], items[6]));
	for (int p = 7; p < TABLE_SLOTS; p++)
		CHECK(!SamePose(poses[p], entry->extra[p - AUTHORED]) && (poses[p].x != 0 || poses[p].z != 0));
}


int main(void)
{
	TestTableEntries();
	TestPlayerCounts();
	TestHumansAtTheBack();
	TestRuleRaceGrid();
	TestRuleArena();
	TestTableNeedsExactMatch();
	printf("start slots: %d table entries OK\n", kNumStartSlotTableEntries);
	return 0;
}
