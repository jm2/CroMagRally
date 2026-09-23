//
// file.h
//

#pragma once

#include "input.h"
#include "main.h"
#include "structs.h"

		/***********************/
		/* RESOURCE STURCTURES */
		/***********************/

			/* Hedr */

typedef struct
{
	int16_t	version;			// 0xaa.bb
	int16_t	numAnims;			// gNumAnims
	int16_t	numJoints;			// gNumJoints
	int16_t	num3DMFLimbs;		// gNumLimb3DMFLimbs
}SkeletonFile_Header_Type;

			/* Bone resource */
			//
			// matches BoneDefinitionType except missing
			// point and normals arrays which are stored in other resources.
			// Also missing other stuff since arent saved anyway.

typedef struct
{
	int32_t				parentBone;			 		// index to previous bone
	uint8_t				name[32];					// text string name for bone
	OGLPoint3D			coord;						// absolute coord (not relative to parent!)
	uint16_t			numPointsAttachedToBone;	// # vertices/points that this bone has
	uint16_t			numNormalsAttachedToBone;	// # vertex normals this bone has
	uint32_t			reserved[8];				// reserved for future use
}File_BoneDefinitionType;



			/* AnHd */

typedef struct
{
	Str32	animName;
	int16_t	numAnimEvents;
}SkeletonFile_AnimHeader_Type;



		/* PREFERENCES */

typedef struct
{
	Byte	numTracksCompleted;
	float	tournamentLapTimes[NUM_RACE_TRACKS][LAPS_PER_RACE];
} TournamentProgression;

typedef struct
{
	Byte	difficulty;
	Byte	splitScreenMode2P;
	Byte	splitScreenMode3P;
	Byte	language;
	Byte	tagDuration;
	Byte	antialiasingLevel;
	Boolean	fullscreen;
	Byte	displayNumMinus1;
	Byte	musicVolumePercent;
	Byte	sfxVolumePercent;
	Byte	raceTimer;

	InputBinding bindings[NUM_CONTROL_NEEDS];
	Boolean	gamepadRumble;

	TournamentProgression tournamentProgression;
	char	playerName[32];

			/* ADDED IN V2 */

	Boolean	cpuFill;					// fill empty multiplayer race slots with CPU cars
}PrefsType;

// The v1 prefs payload: PrefsType before v2 appended its fields. Frozen so that
// v1 files can be upgraded (LoadPrefsFile); never edit it.
typedef struct
{
	Byte	difficulty;
	Byte	splitScreenMode2P;
	Byte	splitScreenMode3P;
	Byte	language;
	Byte	tagDuration;
	Byte	antialiasingLevel;
	Boolean	fullscreen;
	Byte	displayNumMinus1;
	Byte	musicVolumePercent;
	Byte	sfxVolumePercent;
	Byte	raceTimer;

	InputBinding bindings[NUM_CONTROL_NEEDS];
	Boolean	gamepadRumble;

	TournamentProgression tournamentProgression;
	char	playerName[32];
}PrefsTypeV1;

#define PREFS_FOLDER_NAME "CroMagRally"

#define PREFS_MAGIC "CMR Prefs v2   "
#define PREFS_MAGIC_V1 "CMR Prefs v1   "
#define PREFS_FILENAME "Prefs"


// Probably storing more info than necessary, but it's there if we ever want to make a super-detailed scoreboard
typedef struct
{
	int64_t		timestamp;
	float		lapTimes[LAPS_PER_RACE];
	Byte		trackNum;
	Byte		difficulty;
	Byte		gameMode;
	Byte		vehicleType;
	Byte		place;
	Byte		sex;
	Byte		skin;
	char		reserved[32+5];		// pad to 64 bytes & make room for player name (if we want to add this later)
} ScoreboardRecord;

typedef struct
{
	ScoreboardRecord records[NUM_RACE_TRACKS][MAX_RECORDS_PER_TRACK];
} Scoreboard;

#define SCOREBOARD_MAGIC "CMR Scores v0  "
#define SCOREBOARD_MAX_PLACES 16		// file-format bound on ScoreboardRecord.place, independent of MAX_PLAYERS

#define MAX_SAVED_LAP_TIME_SECONDS (24.0f * 60.0f * 60.0f)


		/* COMMAND-LINE OPTIONS */

#define	SMOKE_TEST_MAX_FRAMES		36000		// --smoke-test-frames limit without soak flags (long LAN fill soaks)
#define	SMOKE_SOAK_MAX_FRAMES		100000		// ...and with a practice soak flag (--smoke-cars, --smoke-metrics...)
#define	SMOKE_MIN_FIXED_FPS			9			// --smoke-fixed-fps range: NET_MIN_FPS..MAX_GAME_FPS

typedef struct
{
	int		vsync;
	int		bootToTrack;
	int		smokeTestFrames;
	int		smokeNetPlayers;		// smoke only: host starts once this many players (itself included) joined
	int		smokeNetRefusals;		// smoke only: ...and after refusing this many extra joins as full
	int		smokeCars;				// smoke soak: total cars in a practice race (0 = MAX_PLAYERS)
	int		smokeFixedFPS;			// smoke soak: fixed simulation rate (0 = measured frame time)
	uint32_t	smokeSeed;				// smoke soak: synced RNG seed, if hasSmokeSeed
	bool	hasSmokeSeed;
	bool	smokeAutopilot;			// smoke soak: the CPU AI drives player 1, who still counts as the human
	bool	smokeUntilFinish;		// smoke soak: end the race when player 1 finishes
	bool	smokeMetrics;			// smoke soak: log METRICS lines at the end of the race (race_metrics.h)
	int		smokeLocalPlayers;		// smoke only: race --track as a split-screen multiplayer race with this many humans
	bool	smokeCPUFill;			// smoke only: the local race, or the hosted network race, fills its empty slots with CPU cars
	int		car;
	bool	netHost;
	bool	netJoin;
	bool	netJoinDirect;			// dev/test: --join-address connects straight to a host, skipping LAN discovery
	uint32_t	netJoinAddress;			// IPv4 host address for --join-address (host byte order)
	bool	printMaxNetPlayers;		// dev/test: print how many players one LAN game seats, then quit
	int		display;
	int		windowedWidth;
	int		windowedHeight;
} CommandLineOptions;

//=================================================

SkeletonDefType *LoadSkeletonFile(short skeletonType);

OSErr LoadUserDataFile(const char* path, const char* magic, long payloadLength, Ptr payloadPtr);
OSErr SaveUserDataFile(const char* path, const char* magic, long payloadLength, Ptr payloadPtr);
OSErr LoadPrefsFile(const char* path, PrefsType* prefs, const PrefsType* defaults, Boolean* upgraded);

Boolean SanitizePrefs(PrefsType* prefs, const PrefsType* defaults);
Boolean SanitizeScoreboard(Scoreboard* scoreboard);

OSErr LoadPrefs(void);
void SavePrefs(void);
void LoadPlayfield(FSSpec *specPtr);
void PreloadGameArt(void);
void LoadLevelArt(void);
void LoadCavemanSkins(void);
void DisposeCavemanSkins(void);
void SetDefaultDirectory(void);

void SetDefaultPlayerSaveData(void);
void SavePlayerFile(void);
int GetNumAgesCompleted(void);
int GetNumStagesCompletedInAge(void);
int GetNumTracksCompletedTotal(void);
int GetTrackNumFromAgeStage(int age, int stage);
float GetTotalTournamentTime(void);
void SetPlayerProgression(int numTracksCompleted);

Ptr LoadDataFile(const char* path, long* outLength);
char* LoadTextFile(const char* path, long* outLength);

char* CSVIterator(char** csvCursor, bool* eolOut);

OSErr SaveScoreboardFile(void);
OSErr LoadScoreboardFile(void);

void ValidateResourceSize(Handle handle, int64_t count, size_t elementSize, const char* context);

#define UNPACK_STRUCTS_HANDLE(format, type, n, handle)                         \
do                                                                             \
{                                                                              \
	ValidateResourceSize((Handle) (handle), (n), sizeof(type), #type);          \
	UnpackStructs((format), sizeof(type), (n), *(handle));                     \
} while(0)
