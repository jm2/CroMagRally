// Player-count invariants: per-player masks and tables must cover every player slot,
// and fixed-size lists must refuse entries instead of writing past their end.
#include "game.h"
#include <stdio.h>
#include <stdlib.h>

#define CHECK(condition) do { if (!(condition)) { \
	fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); \
	exit(EXIT_FAILURE); } } while (0)

SuperTileStatus** gSuperTileStatusGrid;
long gNumSuperTilesDeep, gNumSuperTilesWide;

static void TestSuperTilePlayerFlags(void)
{
	// Cover all 16 bits the terrain masks promise (MAX_PLAYERS <= 16 is asserted),
	// not just today's MAX_PLAYERS: an 8-bit field would drop players 8+.
	for (short p = 0; p < 16; p++)
	{
		SuperTileStatus status = {0};
		CHECK(!IsSuperTileUsedByPlayers(&status, -1));
		MarkSuperTilePlayerHere(&status, p);
		CHECK(status.playerHereFlags == (uint16_t)(1u << p));
		CHECK(IsSuperTileUsedByPlayers(&status, -1));
		CHECK(!IsSuperTileUsedByPlayers(&status, p));
		CHECK(IsSuperTileUsedByPlayers(&status, (short)((p + 1) % 16)));
	}

	// Every player on one supertile: skipping any single player leaves it in use.
	SuperTileStatus shared = {0};
	for (short p = 0; p < MAX_PLAYERS; p++)
		MarkSuperTilePlayerHere(&shared, p);
	CHECK(shared.playerHereFlags == (uint16_t)((1u << MAX_PLAYERS) - 1));
	for (short p = 0; p < MAX_PLAYERS; p++)
		CHECK(IsSuperTileUsedByPlayers(&shared, p) == (MAX_PLAYERS > 1));
}

static void TestPlaceTables(void)
{
	// Every place a race can produce keeps its own number sprite and voice line.
	for (int place = 0; place < MAX_PLAYERS; place++)
	{
		CHECK(GetPlaceNumberSprite(place) == INFOBAR_SObjType_Place1 + place);
		CHECK(GetPlaceAnnouncerEffect(place) == EFFECT_1st + place);
	}

	// Past the tables, the number clamps to the last sprite instead of showing the
	// bone-bomb icon, and the announcer stays silent instead of saying "Oh yeah".
	CHECK(GetPlaceNumberSprite(NUM_PLACE_SPRITES) == INFOBAR_SObjType_Place6);
	CHECK(GetPlaceNumberSprite(11) == INFOBAR_SObjType_Place6);
	CHECK(GetPlaceNumberSprite(-1) == INFOBAR_SObjType_Place1);
	CHECK(GetPlaceAnnouncerEffect(NUM_ANNOUNCER_PLACE_LINES) == -1);
	CHECK(GetPlaceAnnouncerEffect(9) == -1);
	CHECK(GetPlaceAnnouncerEffect(-1) == -1);
}

static void TestCollisionListBudget(void)
{
	// A full list refuses new entries instead of writing past its end (an exact-size
	// heap block lets ASan catch any stray write), and keeps the first hits it found.
	enum { kCapacity = 4 };
	CollisionRec* list = calloc(kCapacity, sizeof(*list));
	CHECK(list);
	short numCollisions = 0;
	for (short i = 0; i < kCapacity; i++)
	{
		CollisionRec* rec = AppendCollisionRec(list, &numCollisions, kCapacity);
		CHECK(rec == &list[i] && numCollisions == i + 1);
		rec->targetBox = (Byte)(i + 1);
	}
	for (int i = 0; i < 100; i++)
		CHECK(!AppendCollisionRec(list, &numCollisions, kCapacity));
	CHECK(numCollisions == kCapacity);
	for (short i = 0; i < kCapacity; i++)
		CHECK(list[i].targetBox == i + 1);

	// A count already out of range never yields an entry or moves further.
	numCollisions = kCapacity + 5;
	CHECK(!AppendCollisionRec(list, &numCollisions, kCapacity) && numCollisions == kCapacity + 5);
	numCollisions = -1;
	CHECK(!AppendCollisionRec(list, &numCollisions, kCapacity) && numCollisions == -1);
	free(list);
}

int main(void)
{
	TestSuperTilePlayerFlags();
	TestPlaceTables();
	TestCollisionListBudget();
	puts("Player limit tests passed");
	return EXIT_SUCCESS;
}
