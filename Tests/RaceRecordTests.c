#include "game.h"
#include <stdio.h>
#include <stdlib.h>

#define CHECK(condition) do { if (!(condition)) { \
	fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); \
	exit(EXIT_FAILURE); } } while (0)

PlayerInfoType gPlayerInfo[MAX_PLAYERS];
PrefsType gGamePrefs;
int gDifficulty, gGameMode, gTrackNum;
short gNumLapsThisRace = LAPS_PER_RACE;
Boolean gIsSelfRunningDemo, gUserTamperedWithPhysics;
static int saves;
OSErr SaveScoreboardFile(void) { saves++; return noErr; }

int main(void)
{
	gGameMode = GAME_MODE_MULTIPLAYERRACE;
	gTrackNum = 2;
	gDifficulty = DIFFICULTY_HARD;
	gGamePrefs.difficulty = DIFFICULTY_EASY;
	PlayerInfoType* player = &gPlayerInfo[1];
	player->vehicleType = 3;
	player->place = 2;
	for (int lap = 0; lap < LAPS_PER_RACE; lap++)
		player->lapTimes[lap] = 30 + lap;
	CHECK(SaveRaceTime(1) == 0 && saves == 1);
	const ScoreboardRecord* record = &gScoreboard.records[gTrackNum][0];
	CHECK(record->difficulty == DIFFICULTY_HARD);
	CHECK(gGamePrefs.difficulty == DIFFICULTY_EASY);
	CHECK(record->gameMode == gGameMode && record->trackNum == gTrackNum);
	CHECK(record->vehicleType == 3 && record->place == 2);
	CHECK(!memcmp(record->lapTimes, player->lapTimes, sizeof(record->lapTimes)));

	// Offline sessions initialize the active difficulty from preferences.
	gGameMode = GAME_MODE_PRACTICE;
	gDifficulty = gGamePrefs.difficulty;
	player->lapTimes[0] = 29;
	CHECK(SaveRaceTime(1) == 0 && saves == 2);
	CHECK(record->difficulty == DIFFICULTY_EASY);
	CHECK(record->gameMode == GAME_MODE_PRACTICE);
	CHECK(gScoreboard.records[gTrackNum][1].difficulty == DIFFICULTY_HARD);
	player->isComputer = true;
	CHECK(SaveRaceTime(1) < 0 && saves == 2);
	puts("Race record tests passed");
	return 0;
}
