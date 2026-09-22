// Production snowfall from Effects.c. Linker section GC keeps only MakeSnow and
// the particle-group code it needs; the stubs below stand in for rendering.
#include "game.h"
#include <stdio.h>
#include <stdlib.h>

#define CHECK(condition) do { if (!(condition)) { \
	fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); \
	exit(EXIT_FAILURE); } } while (0)

PlayerInfoType gPlayerInfo[MAX_PLAYERS];
short gNumTotalPlayers;
float gFramesPerSecond, gFramesPerSecondFrac;
Boolean gNetGameInProgress;
short gMyNetworkPlayerNum;
short gNumLocalPlayers;

static Atlas gEffectsAtlas;
Atlas* gAtlases[MAX_SPRITE_GROUPS] = { [SPRITE_GROUP_EFFECTS] = &gEffectsAtlas };

static ObjNode gCars[MAX_PLAYERS];

enum { kFlakesPerBurst = 25 };						// MakeSnow's cap, reached at 60 fps

void* AllocPtr(long size) { return calloc(1, size); }
void SafeDisposePtr(void* ptr) { free(ptr); }
float VisualRandomFloat(void) { return 0.5f; }
float VisualRandomFloat2(void) { return 0.0f; }
void DoFatalAlert(const char* format, ...) { fprintf(stderr, "fatal: %s\n", format); exit(EXIT_FAILURE); }

const AtlasGlyph* Atlas_GetGlyph(const Atlas* atlas, uint32_t codepoint)
{
	static const AtlasGlyph glyph = {0};
	(void) atlas;
	(void) codepoint;
	return &glyph;
}

// The particle group owns its vertex arrays through the geometry object.
MetaObjectPtr MO_CreateNewObjectOfType(uint32_t type, uintptr_t subType, void* data)
{
	(void) type;
	(void) subType;
	MOVertexArrayData* copy = malloc(sizeof(*copy));
	CHECK(copy);
	*copy = *(const MOVertexArrayData*) data;
	return copy;
}

void MO_DisposeObjectReference(MetaObjectPtr obj)
{
	MOVertexArrayData* data = obj;
	free(data->points);
	free(data->uvs);
	free(data->colorsByte);
	free(data->triangles);
	free(data);
}

static void StartRace(short numPlayers, Boolean netGame, short myNetworkPlayerNum, short numLocalPlayers)
{
	memset(gPlayerInfo, 0, sizeof(gPlayerInfo));
	gNumTotalPlayers = numPlayers;
	gNetGameInProgress = netGame;
	gMyNetworkPlayerNum = myNetworkPlayerNum;
	gNumLocalPlayers = numLocalPlayers;
	gFramesPerSecond = 60.0f;
	gFramesPerSecondFrac = 1.0f / 60.0f;
	for (short p = 0; p < numPlayers; p++)
	{
		gPlayerInfo[p].objNode = &gCars[p];
		gPlayerInfo[p].snowParticleGroup = -1;
	}
}

static void Snow(int frames)
{
	for (int i = 0; i < frames; i++)
		MakeSnow();
}

static int CountFlakes(short group)
{
	int n = 0;
	CHECK(group >= 0 && group < MAX_PARTICLE_GROUPS && gParticleGroups[group]);
	for (int i = 0; i < MAX_PARTICLES; i++)
		n += gParticleGroups[group]->isUsed[i] != 0;
	return n;
}

// Snow falls around a camera, so only the players this machine draws make it.
static void CheckOnlyLocalPanesMakeSnow(short numLocalPlayers)
{
	for (short p = 0; p < gNumTotalPlayers; p++)
	{
		Boolean local = gNetGameInProgress ? (p == gMyNetworkPlayerNum) : (p < numLocalPlayers);
		CHECK((gPlayerInfo[p].snowParticleGroup != -1) == local);
		if (!local)
			CHECK(gPlayerInfo[p].snowTimer == 0.0f);
	}
}

static void TestNetworkPlayersGetTheirOwnSnow(void)
{
	// Every network player sees snow, whatever number the host gave it. Snow used to
	// stop at the first car whose timer was not due, so high numbers never got a turn.
	for (short me = 0; me < MAX_PLAYERS; me++)
	{
		StartRace(MAX_PLAYERS, true, me, 1);
		Snow(10);										// a burst every 0.05 s: at least three at 60 fps
		CheckOnlyLocalPanesMakeSnow(1);
		CHECK(CountFlakes(gPlayerInfo[me].snowParticleGroup) >= 3 * kFlakesPerBurst);
		DeleteAllParticleGroups();
		CHECK(gNumActiveParticleGroups == 0);
	}
}

static void TestCPUCarsMakeNoSnow(void)
{
	// A practice race snows for the human only; the CPU cars' cameras are never drawn.
	StartRace(MAX_PLAYERS, false, 0, 1);
	Snow(10);
	CheckOnlyLocalPanesMakeSnow(1);
	CHECK(gNumActiveParticleGroups == 1);
	DeleteAllParticleGroups();

	// Split-screen: each local pane gets snow, the CPU cars still none.
	StartRace(MAX_PLAYERS, false, 0, 2);
	Snow(10);
	CheckOnlyLocalPanesMakeSnow(2);
	DeleteAllParticleGroups();
	CHECK(gNumActiveParticleGroups == 0);
}

int main(void)
{
	TestNetworkPlayersGetTheirOwnSnow();
	TestCPUCarsMakeNoSnow();
	puts("Snow tests passed");
	return EXIT_SUCCESS;
}
