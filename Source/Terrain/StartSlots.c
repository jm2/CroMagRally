/****************************/
/*   	START SLOTS.C		    */
/****************************/
//
// Every player gets a start slot: the playfield's own MyStartCoord item if it has one, else the
// generated table's slot for this map (StartSlotTable.c, tools/gen_start_slots.py), else one
// derived from the authored slots by the procedural rule below. These are pure functions of map
// data and the player setup, so every network peer computes the same slots: table slots are
// exact integers, and the rule uses no libm beyond sqrt (correctly rounded everywhere) and
// truncates to int like the item coordinates it starts from.
//

#include "game.h"
#include "startslots.h"

_Static_assert(MAX_PLAYERS <= START_SLOTS_MAX, "StartSlots_Place fills at most START_SLOTS_MAX slots");


/****************************/
/*    CONSTANTS             */
/****************************/

#define	RULE_ROW_GAP	1300.0				// gap between a wave of slots and the one it copies

		// Car forward (-sin rotY, -cos rotY) for each heading in 1/16 turns (Checkpoints.c aims
		// the same way). Literals rather than libm, so every platform derives the same slots.

#define	S1	0.38268343236508978				// sin 22.5
#define	S2	0.70710678118654752				// sin 45
#define	S3	0.92387953251128674				// sin 67.5

static const double kHeading[16][2] =
{
	{ 0.0, -1.0}, {-S1, -S3}, {-S2, -S2}, {-S3, -S1}, {-1.0, 0.0}, {-S3,  S1}, {-S2,  S2}, {-S1,  S3},
	{ 0.0,  1.0}, { S1,  S3}, { S2,  S2}, { S3,  S1}, { 1.0, 0.0}, { S3, -S1}, { S2, -S2}, { S1, -S3},
};


/********************** HEADING TO ROT Y **************************/
//
// Exactly the expression FindPlayerStartCoordItems has always used for MyStartCoord parm[1].
//

float StartSlot_RotY(int rot16)
{
	return PI2 * ((float)rot16 * (1.0f/16.0f));
}


/********************** COUNT AUTHORED **************************/
//
// How many players from 0 up have an authored slot: the slots the procedural rule copies.
//

int StartSlots_CountAuthored(const bool authored[], int numSlots)
{
	int n = 0;

	while (n < numSlots && authored[n])
		n++;
	return n;
}


/********************** FIND TABLE ENTRY **************************/
//
// The table entry for this map, if the map's authored slots of this set are exactly players
// 0-5 with exactly the coordinates and headings the table was generated from, and the table
// has a slot for every other player. Anything else is a modified map: use the procedural rule.
//

static const StartSlotTableEntry* FindTableEntry(StartSlotSet set, int mapUnitWidth, int mapUnitDepth,
												const StartSlot items[], const bool authored[], int numSlots)
{
	if (numSlots > START_SLOT_TABLE_AUTHORED + START_SLOT_TABLE_EXTRA)
		return NULL;

	for (int p = 0; p < numSlots; p++)
	{
		if (authored[p] != (p < START_SLOT_TABLE_AUTHORED))
			return NULL;
	}

	for (int e = 0; e < kNumStartSlotTableEntries; e++)
	{
		const StartSlotTableEntry* entry = &kStartSlotTable[e];
		bool match = entry->set == set && entry->mapUnitWidth == mapUnitWidth && entry->mapUnitDepth == mapUnitDepth;

		for (int p = 0; match && p < START_SLOT_TABLE_AUTHORED; p++)
		{
			match = items[p].x == entry->authored[p].x
				&& items[p].z == entry->authored[p].z
				&& items[p].rot16 == entry->authored[p].rot16;
		}
		if (match)
			return entry;
	}
	return NULL;
}


/********************** RING TURN **************************/
//
// cos and sin of the battle rule's half-slot turn, from their Taylor series. Only basic
// arithmetic (which the build keeps uncontracted), so every platform derives the same bits;
// libm's cos and sin may differ in the last one. 0 < theta <= pi, where 15 terms are accurate
// to double precision.
//

static void RingTurn(double theta, double* c, double* s)
{
	const double t2 = theta * theta;
	double cTerm = 1.0, sTerm = theta;

	*c = cTerm;
	*s = sTerm;
	for (int k = 1; k <= 15; k++)
	{
		cTerm *= -t2 / ((2 * k - 1) * (2 * k));
		sTerm *= -t2 / ((2 * k) * (2 * k + 1));
		*c += cTerm;
		*s += sTerm;
	}
}


/********************** RULE COORD **************************/
//
// Truncate toward zero (as a float to int assignment does) and keep the slot on the playfield.
//

static int RuleCoord(double v, int mapUnitSize)
{
	if (v < 0.0)
		return 0;
	if (mapUnitSize > 0 && v > mapUnitSize - 1)
		return mapUnitSize - 1;
	return (int) v;
}


/********************** RULE SOURCE **************************/
//
// The authored slot the rule derives player p's slot from, and how many waves out it goes. On
// the battle ring and the race grid that is slot p % n, one wave per n players. In Capture the
// Flag it is a teammate (even players red, odd green): p is the (p / 2)th player of its team,
// which has authored slots team, team + 2, ... below n, and its members are copied in turn.
//

static void RuleSource(StartSlotSet set, int n, int p, int* src, int* wave)
{
	if (set == START_SLOT_SET_CTF)
	{
		const int team = p & 1;
		const int members = (n - team + 1) / 2;			// >= 1: the CTF rule needs n >= 2
		const int k = p / 2;
		*src = team + 2 * (k % members);
		*wave = k / members;
	}
	else
	{
		*src = p % n;
		*wave = p / n;
	}
}


/********************** FILL BY RULE **************************/
//
// The procedural rule, for maps the table doesn't know. It derives each missing player's slot
// from an authored slot (RuleSource), one "wave" further out each time the authored slots have
// all been copied:
//   race:   repeat the authored grid behind itself, one row gap further back per wave, keeping
//           the lanes and headings, so slot n+k is behind authored slot k
//   CTF:    a parallel column beside each team's line, on the side facing the arena, copying
//           the team's own slots; a team with a single slot lines up in front of it
//   battle: a wider ring around the authored cluster, rotated half a slot
// (From the 12-player prototype.)
//

static void FillByRule(StartSlotSet set, int mapUnitWidth, int mapUnitDepth,
						const StartSlot items[], const bool authored[], int numSlots, StartSlotPose poses[])
{
	const int n = StartSlots_CountAuthored(authored, numSlots);
	if (n == 0)												// nothing to derive from: leave them at (0,0) as always
		return;
	if (set == START_SLOT_SET_CTF && n < 2)					// no slot for one team: use the battle rule
		set = START_SLOT_SET_BATTLE;

	double cx = 0, cz = 0;
	for (int i = 0; i < n; i++)
	{
		cx += items[i].x;
		cz += items[i].z;
	}
	cx /= n;
	cz /= n;

	if (set == START_SLOT_SET_RACE)
	{
		const double fx = kHeading[items[0].rot16 & 15][0];
		const double fz = kHeading[items[0].rot16 & 15][1];
		double dmin = 0, dmax = 0;
		for (int i = 0; i < n; i++)
		{
			const double d = (items[i].x - cx) * fx + (items[i].z - cz) * fz;
			dmin = (i == 0 || d < dmin) ? d : dmin;
			dmax = (i == 0 || d > dmax) ? d : dmax;
		}
		const double shift = (dmax - dmin) + RULE_ROW_GAP;

		for (int p = n; p < numSlots; p++)
		{
			if (authored[p])
				continue;
			int from, wave;
			RuleSource(set, n, p, &from, &wave);
			const StartSlot* src = &items[from];
			poses[p].x = RuleCoord(src->x - fx * shift * wave, mapUnitWidth);
			poses[p].z = RuleCoord(src->z - fz * shift * wave, mapUnitDepth);
			poses[p].rotY = StartSlot_RotY(src->rot16);
		}
	}
	else if (set == START_SLOT_SET_CTF)
	{
		double stepX[2], stepZ[2], stepLen[2];						// each team's wave step

		for (int team = 0; team < 2; team++)
		{
			const int members = (n - team + 1) / 2;
			const StartSlot* first = &items[team];
			if (members == 1)										// no line: in front of the lone slot
			{
				stepX[team] = kHeading[first->rot16 & 15][0];
				stepZ[team] = kHeading[first->rot16 & 15][1];
				stepLen[team] = RULE_ROW_GAP;
				continue;
			}
			const StartSlot* a = &items[team + 2 * (members - 2)];	// the team's last two slots
			const StartSlot* b = &items[team + 2 * (members - 1)];
			const double sx = b->x - a->x;							// step along the team's line
			const double sz = b->z - a->z;
			double len = sqrt(sx * sx + sz * sz);
			if (len < 1.0)
				len = 1.0;
			double px = -sz / len, pz = sx / len;					// perpendicular to it
			if ((cx - first->x) * px + (cz - first->z) * pz < 0)	// towards the other team
			{
				px = -px;
				pz = -pz;
			}
			stepX[team] = px;
			stepZ[team] = pz;
			stepLen[team] = len;
		}

		for (int p = n; p < numSlots; p++)
		{
			if (authored[p])
				continue;
			const int team = p & 1;
			int from, wave;
			RuleSource(set, n, p, &from, &wave);
			const StartSlot* src = &items[from];
			poses[p].x = RuleCoord(src->x + stepX[team] * stepLen[team] * wave, mapUnitWidth);
			poses[p].z = RuleCoord(src->z + stepZ[team] * stepLen[team] * wave, mapUnitDepth);
			poses[p].rotY = StartSlot_RotY(src->rot16);
		}
	}
	else
	{
		const float theta = PI2 / (float) (2 * n);										// half a slot
		double c, s;
		RingTurn(theta, &c, &s);

		for (int p = n; p < numSlots; p++)
		{
			if (authored[p])
				continue;
			int from, wave;
			RuleSource(set, n, p, &from, &wave);
			const StartSlot* src = &items[from];
			double dx = src->x - cx, dz = src->z - cz;
			const double r = sqrt(dx * dx + dz * dz);
			const double scale = r > 1.0 ? (r + RULE_ROW_GAP * wave) / r : 1.0;
			dx *= scale;
			dz *= scale;
			poses[p].x = RuleCoord(cx + dx * c + dz * s, mapUnitWidth);
			poses[p].z = RuleCoord(cz - dx * s + dz * c, mapUnitDepth);
			poses[p].rotY = StartSlot_RotY(src->rot16) + theta;
		}
	}
}


/********************** FILL START SLOTS **************************/
//
// items[p] is player p's MyStartCoord item where authored[p]. Fills poses[0 .. numSlots-1]:
// authored players keep their item; the others take the table's slots when this map and set
// match a table entry, else the procedural rule's. Returns the table entry used, or NULL when
// nothing was missing or the rule filled the gaps.
//

const StartSlotTableEntry* StartSlots_Fill(StartSlotSet set, int mapUnitWidth, int mapUnitDepth,
										const StartSlot items[], const bool authored[], int numSlots,
										StartSlotPose poses[])
{
	bool missing = false;

	for (int p = 0; p < numSlots; p++)
	{
		if (authored[p])
			poses[p] = (StartSlotPose) { items[p].x, items[p].z, StartSlot_RotY(items[p].rot16) };
		else
		{
			poses[p] = (StartSlotPose) { 0, 0, 0 };
			missing = true;
		}
	}
	if (!missing)
		return NULL;

	const StartSlotTableEntry* entry = FindTableEntry(set, mapUnitWidth, mapUnitDepth, items, authored, numSlots);
	if (entry)
	{
		for (int p = START_SLOT_TABLE_AUTHORED; p < numSlots; p++)
		{
			const StartSlot* s = &entry->extra[p - START_SLOT_TABLE_AUTHORED];
			poses[p] = (StartSlotPose) { s->x, s->z, StartSlot_RotY(s->rot16) };
		}
		return entry;
	}

	FillByRule(set, mapUnitWidth, mapUnitDepth, items, authored, numSlots, poses);
	return NULL;
}


/********************** KEEP HUMANS AT THE BACK **************************/
//
// The shipped grids put player 0, the human in a one-player race, in the rear row. Once CPU
// slots exist behind it, swap each human among the first authoredCount players into the
// matching slot of the rearmost wave, so no CPU starts queued behind a car that hasn't moved
// yet. Does nothing while everyone fits on the authored grid.
//

void StartSlots_KeepHumansAtBack(StartSlotPose poses[], const bool isComputer[], int numPlayers, int authoredCount)
{
	if (authoredCount <= 0 || numPlayers <= authoredCount)
		return;

	const int lastWave = ((numPlayers - 1) / authoredCount) * authoredCount;

	for (int p = 0; p < authoredCount; p++)
	{
		const int q = lastWave + p;
		if (q >= numPlayers || isComputer[p] || !isComputer[q])
			continue;

		const StartSlotPose human = poses[p];
		poses[p] = poses[q];
		poses[q] = human;
	}
}


/********************** SLOT SET FOR A GAME MODE **************************/
//
// Capture the Flag uses the arena's CTF slots (MyStartCoord parm[3] bit 0), Tag and Survival its
// other slots (the battle ring); every other mode races on the track's grid.
//

StartSlotSet StartSlots_SetForGameMode(int gameMode)
{
	switch (gameMode)
	{
		case	GAME_MODE_CAPTUREFLAG:
				return START_SLOT_SET_CTF;

		case	GAME_MODE_TAG1:
		case	GAME_MODE_TAG2:
		case	GAME_MODE_SURVIVAL:
				return START_SLOT_SET_BATTLE;

		default:
				return START_SLOT_SET_RACE;
	}
}


/********************** PLACE PLAYERS **************************/
//
// FindPlayerStartCoordItems (Terrain2.c) without the globals. Scans the playfield's items for
// this mode's MyStartCoord items (parm[0] player, parm[1] heading in 1/16 turns, parm[3] bit 0
// set only in Capture the Flag; players numSlots and up are skipped), gives every slot a pose
// (StartSlots_Fill), and on a race grid moves the humans among players 0 .. numPlayers-1 to the
// back (StartSlots_KeepHumansAtBack). Returns -1, or the player number of a duplicate item, in
// which case poses are not filled.
//

int StartSlots_Place(const TerrainItemEntryType itemList[], long numItems, int gameMode,
					int mapUnitWidth, int mapUnitDepth, const bool isComputer[], int numPlayers,
					int numSlots, StartSlotPose poses[])
{
	StartSlot	items[START_SLOTS_MAX];
	bool		authored[START_SLOTS_MAX];
	const bool	ctf = gameMode == GAME_MODE_CAPTUREFLAG;

	if (numSlots > START_SLOTS_MAX)
		numSlots = START_SLOTS_MAX;
	if (numPlayers > numSlots)
		numPlayers = numSlots;

	for (int p = 0; p < numSlots; p++)
	{
		items[p] = (StartSlot) { 0, 0, 0 };
		authored[p] = false;
	}

			/* SCAN FOR THIS MODE'S "START COORD" ITEMS */

	for (long i = 0; i < numItems; i++)
	{
		const TerrainItemEntryType* item = &itemList[i];

		if (item->type != MAP_ITEM_MYSTARTCOORD)
			continue;
		if (((item->parm[3] & 1) != 0) != ctf)							// CTF slots only in CTF, the others otherwise
			continue;

		const int p = item->parm[0];										// player # is in parm 0
		if (p >= numSlots)													// skip illegal player #'s
			continue;
		if (authored[p])
			return p;

		items[p] = (StartSlot) { (int) item->x, (int) item->y, item->parm[1] };
		authored[p] = true;
	}

			/* EVERY SLOT, AND HUMANS AT THE BACK OF A RACE GRID */

	const StartSlotSet set = StartSlots_SetForGameMode(gameMode);

	StartSlots_Fill(set, mapUnitWidth, mapUnitDepth, items, authored, numSlots, poses);
	if (set == START_SLOT_SET_RACE)
		StartSlots_KeepHumansAtBack(poses, isComputer, numPlayers, StartSlots_CountAuthored(authored, numSlots));
	return -1;
}
