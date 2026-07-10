#include "game.h"
#include "net_validation.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

// Keep every check active in Release/RelWithDebInfo too; the standard assert macro disappears
// under NDEBUG, which would turn this executable into a false-positive test.
#define assert(condition) do { if (!(condition)) { \
	fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #condition); \
	exit(EXIT_FAILURE); \
} } while (0)

static void TestEnvelopeValidation(void)
{
	NSpJoinRequestMessage join = {0};
	join.header.what = kNSpJoinRequest;
	join.header.to = kNSpHostID;
	join.header.messageLen = sizeof(join);
	assert(NetValidateInboundEnvelope(kNetInbound_Host, &join.header));

	join.header.messageLen = sizeof(NSpMessageHeader);
	assert(!NetValidateInboundEnvelope(kNetInbound_Host, &join.header));

	NSpGameTerminatedMessage terminated = {0};
	terminated.header.what = kNSpGameTerminated;
	terminated.header.to = kNSpAllPlayers;
	terminated.header.messageLen = sizeof(terminated);
	assert(!NetValidateInboundEnvelope(kNetInbound_Host, &terminated.header));
	assert(NetValidateInboundEnvelope(kNetInbound_Client, &terminated.header));

	NetClientControlInfoMessageType control = {0};
	control.h.what = kNetClientControlInfoMessage;
	control.h.to = kNSpAllPlayers;
	control.h.messageLen = sizeof(control);
	assert(!NetValidateInboundEnvelope(kNetInbound_Host, &control.h));

	NSpMessageHeader unknown = {.what = 'nope', .messageLen = sizeof(NSpMessageHeader)};
	assert(!NetValidateInboundEnvelope(kNetInbound_Host, &unknown));
	assert(!NetValidateInboundEnvelope(kNetInbound_Client, &unknown));

	NSpJoinApprovedMessage approved = {0};
	approved.header.what = kNSpJoinApproved;
	approved.header.messageLen = sizeof(approved);
	approved.header.to = kNSpHostID + MAX_CLIENTS - 1;
	assert(NetValidateInboundEnvelope(kNetInbound_Client, &approved.header));
	approved.header.to = kNSpHostID + MAX_CLIENTS;
	assert(!NetValidateInboundEnvelope(kNetInbound_Client, &approved.header));
	approved.header.to = kNSpHostID;
	assert(!NetValidateInboundEnvelope(kNetInbound_Client, &approved.header));
	approved.header.to = kNSpAllPlayers;
	assert(!NetValidateInboundEnvelope(kNetInbound_Client, &approved.header));
}

static NetPlayerCharTypeMessage ValidCharMessage(void)
{
	NetPlayerCharTypeMessage message = {0};
	message.playerNum = 1;
	message.vehicleType = 0;
	message.sex = 0;
	message.skin = 0;
	message.refreshRate = 60;
	message.connectionType = 1;
	return message;
}

static void TestCharacterValidation(void)
{
	NetPlayerCharTypeMessage message = ValidCharMessage();
	assert(NetValidatePlayerCharPayload(&message, 1, 4));

	message.playerNum = 2;
	assert(!NetValidatePlayerCharPayload(&message, 1, 4));
	message = ValidCharMessage();
	message.vehicleType = -1;
	assert(!NetValidatePlayerCharPayload(&message, 1, 4));
	message = ValidCharMessage();
	message.sex = -1;
	assert(!NetValidatePlayerCharPayload(&message, 1, 4));
	message = ValidCharMessage();
	message.skin = NUM_CAVEMAN_SKINS;
	assert(!NetValidatePlayerCharPayload(&message, 1, 4));
	message = ValidCharMessage();
	message.connectionType = 2;
	assert(!NetValidatePlayerCharPayload(&message, 1, 4));
}

static void TestConfigValidation(void)
{
	NetConfigMessage message = {0};
	message.gameMode = GAME_MODE_MULTIPLAYERRACE;
	message.age = 0;
	message.trackNum = 0;
	message.playerNum = 1;
	message.numPlayers = 2;
	message.difficulty = 0;
	message.targetFPS = 60;
	assert(NetValidateConfigPayload(&message));

	message.numPlayers = MAX_LOCAL_PLAYERS + 1;
	assert(!NetValidateConfigPayload(&message));
	message.numPlayers = 2;
	message.playerNum = 2;
	assert(!NetValidateConfigPayload(&message));
	message.playerNum = 1;
	message.trackNum = NUM_TRACKS;
	assert(!NetValidateConfigPayload(&message));

	message.trackNum = NUM_RACE_TRACKS;
	assert(!NetValidateConfigPayload(&message));
	message.gameMode = GAME_MODE_TAG1;
	assert(NetValidateConfigPayload(&message));
	message.trackNum = NUM_RACE_TRACKS - 1;
	assert(!NetValidateConfigPayload(&message));
	message.gameMode = GAME_MODE_CAPTUREFLAG;
	message.trackNum = NUM_TRACKS - 1;
	assert(NetValidateConfigPayload(&message));
}

static void TestSyncMaskValidation(void)
{
	const uint32_t hostAndClient1 = (1u << 0) | (1u << 1);
	const uint32_t departedClient2 = 1u << 2;

	assert(NetAreAllActivePlayersSynced(hostAndClient1, hostAndClient1));
	assert(!NetAreAllActivePlayersSynced(1u << 0, hostAndClient1));
	assert(NetAreAllActivePlayersSynced(hostAndClient1 | departedClient2, hostAndClient1));
	assert(NetRetainActiveSyncBits(hostAndClient1 | departedClient2, hostAndClient1) == hostAndClient1);
}

static NetClientControlInfoMessageType ValidControlMessage(void)
{
	NetClientControlInfoMessageType message = {0};
	message.playerNum = 1;
	message.controlBits = 1u << kControlBit_Forward;
	message.analogSteering = (OGLVector2D){.x = -1.0f, .y = 1.0f};
	return message;
}

static void TestControlValidation(void)
{
	NetClientControlInfoMessageType message = ValidControlMessage();
	assert(NetValidateClientControlPayload(&message, 1, 4));

	message.playerNum = 2;
	assert(!NetValidateClientControlPayload(&message, 1, 4));
	message = ValidControlMessage();
	message.pauseState = 2;
	assert(!NetValidateClientControlPayload(&message, 1, 4));
	message = ValidControlMessage();
	message.controlBits = 1u << NUM_CONTROL_BITS;
	assert(!NetValidateClientControlPayload(&message, 1, 4));
	message = ValidControlMessage();
	message.analogSteering.x = NAN;
	assert(!NetValidateClientControlPayload(&message, 1, 4));
	message = ValidControlMessage();
	message.analogSteering.y = 1.01f;
	assert(!NetValidateClientControlPayload(&message, 1, 4));
}

static NetHostControlInfoMessageType ValidHostControlMessage(void)
{
	NetHostControlInfoMessageType message = {0};
	message.fps = 60.0f;
	message.fpsFrac = 1.0f / 60.0f;
	message.frameCounter = 100;
	return message;
}

static void TestHostControlValidation(void)
{
	NetHostControlInfoMessageType message = ValidHostControlMessage();
	assert(NetValidateHostControlPayload(&message, 4));
	assert(!NetValidateHostControlPayload(NULL, 4));
	assert(!NetValidateHostControlPayload(&message, 0));
	assert(!NetValidateHostControlPayload(&message, MAX_LOCAL_PLAYERS + 1));

	message.fps = 9.0f;
	message.fpsFrac = 1.0f / 9.0f;
	assert(NetValidateHostControlPayload(&message, 4));
	message.fps = 1000.0f;
	message.fpsFrac = 0.001f;
	assert(NetValidateHostControlPayload(&message, 4));
	message = ValidHostControlMessage();
	message.fps = NAN;
	assert(!NetValidateHostControlPayload(&message, 4));
	message = ValidHostControlMessage();
	message.fps = INFINITY;
	assert(!NetValidateHostControlPayload(&message, 4));
	message = ValidHostControlMessage();
	message.fps = 8.99f;
	assert(!NetValidateHostControlPayload(&message, 4));
	message = ValidHostControlMessage();
	message.fpsFrac = 0.0f;
	assert(!NetValidateHostControlPayload(&message, 4));
	message = ValidHostControlMessage();
	message.fpsFrac = -INFINITY;
	assert(!NetValidateHostControlPayload(&message, 4));
	message = ValidHostControlMessage();
	message.fpsFrac = 0.02f;
	assert(!NetValidateHostControlPayload(&message, 4));

	message = ValidHostControlMessage();
	message.controlBits[MAX_PLAYERS - 1] = 1u << NUM_CONTROL_BITS;
	assert(!NetValidateHostControlPayload(&message, 4));
	message = ValidHostControlMessage();
	message.controlBitsNew[0] = 1u << NUM_CONTROL_BITS;
	assert(!NetValidateHostControlPayload(&message, 4));
	message = ValidHostControlMessage();
	message.controlBitsNew[0] = 1u << kControlBit_Forward;
	assert(NetValidateHostControlPayload(&message, 4));

	message = ValidHostControlMessage();
	message.analogSteering[0] = (OGLVector2D){-1.0f, 1.0f};
	assert(NetValidateHostControlPayload(&message, 4));
	message.analogSteering[0].x = -1.01f;
	assert(!NetValidateHostControlPayload(&message, 4));
	message = ValidHostControlMessage();
	message.analogSteering[0].y = NAN;
	assert(!NetValidateHostControlPayload(&message, 4));

	message = ValidHostControlMessage();
	message.syncPos[0] = (OGLPoint3D){-1000000.0f, 1000000.0f, 0.0f};
	message.syncRotY[0] = -1000000.0f;
	assert(NetValidateHostControlPayload(&message, 4));
	message.syncPos[0].z = 1000001.0f;
	assert(!NetValidateHostControlPayload(&message, 4));
	message = ValidHostControlMessage();
	message.syncPos[0].y = INFINITY;
	assert(!NetValidateHostControlPayload(&message, 4));
	message = ValidHostControlMessage();
	message.syncRotY[0] = NAN;
	assert(!NetValidateHostControlPayload(&message, 4));
	message = ValidHostControlMessage();
	message.syncRotY[0] = -1000001.0f;
	assert(!NetValidateHostControlPayload(&message, 4));

	message = ValidHostControlMessage();
	message.pauseState[0] = 2;
	assert(!NetValidateHostControlPayload(&message, 4));
	message = ValidHostControlMessage();
	message.inputFlags[0] = INPUT_FLAG_SUBSTITUTED | INPUT_FLAG_COALESCED;
	assert(NetValidateHostControlPayload(&message, 4));
	message.inputFlags[0] |= 0x04;
	assert(!NetValidateHostControlPayload(&message, 4));
	message = ValidHostControlMessage();
	message.queueDepth[0] = NET_INPUT_QUEUE_SIZE - 1;
	message.targetDepth[0] = NET_MAX_INPUT_DEPTH;
	assert(NetValidateHostControlPayload(&message, 4));
	message.queueDepth[0] = NET_INPUT_QUEUE_SIZE;
	assert(!NetValidateHostControlPayload(&message, 4));
	message = ValidHostControlMessage();
	message.targetDepth[0] = NET_MAX_INPUT_DEPTH + 1;
	assert(!NetValidateHostControlPayload(&message, 4));

	message = ValidHostControlMessage();
	message.eventCount = 1;
	message.events[0] = (NetFrameEvent)
	{
		.effectiveFrame = message.frameCounter + NET_MAX_EVENT_LEAD,
		.type = kEvBecomeBot,
		.playerNum = 3,
	};
	assert(NetValidateHostControlPayload(&message, 4));
	message.events[0].effectiveFrame++;
	assert(!NetValidateHostControlPayload(&message, 4));
	message.events[0].effectiveFrame = message.frameCounter - 1;
	assert(!NetValidateHostControlPayload(&message, 4));
	message.events[0].effectiveFrame = message.frameCounter;
	message.events[0].type = kEvReserved;
	assert(!NetValidateHostControlPayload(&message, 4));
	message.events[0].type = kEvUnpauseForce;
	message.events[0].playerNum = -1;
	assert(!NetValidateHostControlPayload(&message, 4));
	message.events[0].playerNum = 4;
	assert(!NetValidateHostControlPayload(&message, 4));
	message.events[0].playerNum = 0;
	message.events[0].pad = 1;
	assert(!NetValidateHostControlPayload(&message, 4));

	message = ValidHostControlMessage();
	message.frameCounter = UINT32_MAX - 5;
	message.eventCount = 1;
	message.events[0] = (NetFrameEvent)
	{
		.effectiveFrame = 3,
		.type = kEvUnpauseForce,
		.playerNum = 0,
	};
	assert(NetValidateHostControlPayload(&message, 4));

	message = ValidHostControlMessage();
	message.eventCount = 2;
	for (int i = 0; i < 2; i++)
	{
		message.events[i] = (NetFrameEvent)
		{
			.effectiveFrame = message.frameCounter + 1,
			.type = kEvBecomeBot,
			.playerNum = 1,
		};
	}
	assert(!NetValidateHostControlPayload(&message, 4));

	message = ValidHostControlMessage();
	message.eventCount = NET_MAX_PENDING_EVENTS;
	for (int i = 0; i < NET_MAX_PENDING_EVENTS; i++)
	{
		message.events[i] = (NetFrameEvent)
		{
			.effectiveFrame = message.frameCounter + NET_MAX_EVENT_LEAD,
			.type = i < 4 ? kEvBecomeBot : kEvUnpauseForce,
			.playerNum = i % 4,
		};
	}
	assert(NetValidateHostControlPayload(&message, 4));
	message.eventCount = NET_MAX_PENDING_EVENTS + 1;
	assert(!NetValidateHostControlPayload(&message, 4));

	message = ValidHostControlMessage();
	message.events[0] = (NetFrameEvent){.effectiveFrame = UINT32_MAX, .type = 255, .playerNum = -1, .pad = 1};
	message.randomSeed = UINT32_MAX;
	message.simTick = UINT32_MAX;
	message.ackInputSeq[0] = UINT32_MAX;
	assert(NetValidateHostControlPayload(&message, 4));
}

static void TestDeterministicEventMath(void)
{
	float first = DeterministicEventFloat(123, kDeterministicEvent_LandMineHit, 2, 0);
	float repeated = DeterministicEventFloat(123, kDeterministicEvent_LandMineHit, 2, 0);
	float nextSample = DeterministicEventFloat(123, kDeterministicEvent_LandMineHit, 2, 1);
	assert(first == repeated);
	assert(first >= 0.0f && first < 1.0f);
	assert(nextSample >= 0.0f && nextSample < 1.0f);
	assert(first != nextSample);
	assert(DeterministicEventFloat(123, kDeterministicEvent_GoddessBolt, 2, 2)
		!= DeterministicEventFloat(123, kDeterministicEvent_GoddessBolt, 2, 3));
	assert(DeterministicEventFloat(123, kDeterministicEvent_GoddessBolt, 2, 3)
		!= DeterministicEventFloat(123, kDeterministicEvent_GoddessBolt, 2, 4));

	uint32_t firstBits = 0;
	uint32_t nextSampleBits = 0;
	memcpy(&firstBits, &first, sizeof(firstBits));
	memcpy(&nextSampleBits, &nextSample, sizeof(nextSampleBits));
	assert(firstBits == 0x3DA73F48u);
	assert(nextSampleBits == 0x3F1420CBu);

	assert(DeterministicUnorderedPairKey(1, 4) == DeterministicUnorderedPairKey(4, 1));
	assert(DeterministicPairKey(1, 4) != DeterministicPairKey(4, 1));
	assert(DeterministicFrameKey(false, 99, 7) == 7);
	assert(DeterministicFrameKey(true, 0, 7) == 7);
	assert(DeterministicFrameKey(true, 99, 7) == 98);
	assert(DeterministicStableFloat(kDeterministicEvent_FlagPlace, 7, 0)
		== DeterministicEventFloat(0, kDeterministicEvent_FlagPlace, 7, 0));
}

static void TestCursorClamping(void)
{
	char buffer[8] = {0};
	char* cursor = buffer;
	size_t remaining = sizeof(buffer);

	AdvanceTextCursor(3, &cursor, &remaining);
	assert(cursor == buffer + 3 && remaining == 5);
	AdvanceTextCursor(100, &cursor, &remaining);
	assert(cursor == buffer + 7 && remaining == 1);
	AdvanceTextCursor(100, &cursor, &remaining);
	assert(cursor == buffer + 7 && remaining == 1);
	remaining = 0;
	AdvanceTextCursor(1, &cursor, &remaining);
	assert(cursor == buffer + 7 && remaining == 0);
}

static int FormatForTest(const char* text, char* buf, size_t bufSize, const char* format, ...)
{
	va_list args;
	va_start(args, format);
	int result = VFormatTextWithPlaceholder(text, buf, bufSize, format, args);
	va_end(args);
	return result;
}

static void TestPlaceholderFormatting(void)
{
	char exact[8];
	assert(FormatForTest("A#Z", exact, sizeof(exact), "%s", "12345") == 7);
	assert(strcmp(exact, "A12345Z") == 0);

	char shortBuffer[5];
	assert(FormatForTest("A#Z", shortBuffer, sizeof(shortBuffer), "%s", "12345") == 4);
	assert(strcmp(shortBuffer, "A123") == 0);

	char oneByte[1];
	assert(FormatForTest("A#Z", oneByte, sizeof(oneByte), "%s", "12345") == 0);
	assert(oneByte[0] == '\0');

	assert(FormatForTest("plain", NULL, 0, "%d", 1) == 0);
}

static PrefsType ValidPrefs(void)
{
	PrefsType prefs = {0};
	prefs.difficulty = DIFFICULTY_MEDIUM;
	prefs.splitScreenMode2P = SPLITSCREEN_MODE_2P_TALL;
	prefs.splitScreenMode3P = SPLITSCREEN_MODE_3P_TALL;
	prefs.language = LANGUAGE_ENGLISH;
	prefs.tagDuration = 3;
	prefs.antialiasingLevel = 2;
	prefs.fullscreen = true;
	prefs.musicVolumePercent = 60;
	prefs.sfxVolumePercent = 60;
	prefs.raceTimer = 1;
	prefs.gamepadRumble = true;
	prefs.bindings[0].key[0] = SDL_SCANCODE_SPACE;
	prefs.bindings[0].pad[0] = (PadBinding){kInputTypeButton, SDL_GAMEPAD_BUTTON_SOUTH};
	prefs.bindings[1].pad[0] = (PadBinding){kInputTypeAxisPlus, SDL_GAMEPAD_AXIS_LEFTX};
	SDL_strlcpy(prefs.playerName, "PLAYER", sizeof(prefs.playerName));
	return prefs;
}

static void TestPrefsSanitization(void)
{
	PrefsType defaults = ValidPrefs();
	PrefsType prefs = defaults;
	assert(!SanitizePrefs(&prefs, &defaults));

#define CHECK_REPAIR(field, badValue) \
	do \
	{ \
		prefs = defaults; \
		prefs.field = (badValue); \
		assert(SanitizePrefs(&prefs, &defaults)); \
		assert(prefs.field == defaults.field); \
	} while (0)

	CHECK_REPAIR(difficulty, NUM_DIFFICULTIES);
	CHECK_REPAIR(splitScreenMode2P, SPLITSCREEN_MODE_3P_TALL);
	CHECK_REPAIR(splitScreenMode3P, SPLITSCREEN_MODE_2P_TALL);
	CHECK_REPAIR(language, NUM_LANGUAGES);
	CHECK_REPAIR(tagDuration, 0);
	CHECK_REPAIR(antialiasingLevel, 31);
	CHECK_REPAIR(fullscreen, 2);
	CHECK_REPAIR(musicVolumePercent, 61);
	CHECK_REPAIR(sfxVolumePercent, 255);
	CHECK_REPAIR(raceTimer, 3);
	CHECK_REPAIR(gamepadRumble, 2);
	CHECK_REPAIR(tournamentProgression.numTracksCompleted, NUM_RACE_TRACKS + 1);

#undef CHECK_REPAIR

	prefs = defaults;
	prefs.tournamentProgression.tournamentLapTimes[0][0] = NAN;
	prefs.bindings[0].key[0] = -1;
	prefs.bindings[0].pad[0] = (PadBinding){127, -1};
	prefs.bindings[0].mouseButton = -1;
	SDL_memset(prefs.playerName, 'X', sizeof(prefs.playerName));
	assert(SanitizePrefs(&prefs, &defaults));
	assert(prefs.tournamentProgression.tournamentLapTimes[0][0] == 0);
	assert(prefs.bindings[0].key[0] == SDL_SCANCODE_SPACE);
	assert(prefs.bindings[0].pad[0].type == kInputTypeButton);
	assert(prefs.bindings[0].mouseButton == 0);
	assert(strcmp(prefs.playerName, "PLAYER") == 0);

	prefs = defaults;
	prefs.bindings[kNeed_UIBack].key[0] = SDL_SCANCODE_F1;	// valid, but UI bindings are protected
	prefs.bindings[kNeed_Forward].key[MAX_USER_BINDINGS_PER_NEED] = SDL_SCANCODE_F2;
	assert(SanitizePrefs(&prefs, &defaults));
	assert(prefs.bindings[kNeed_UIBack].key[0] == defaults.bindings[kNeed_UIBack].key[0]);
	assert(prefs.bindings[kNeed_Forward].key[MAX_USER_BINDINGS_PER_NEED]
		== defaults.bindings[kNeed_Forward].key[MAX_USER_BINDINGS_PER_NEED]);
}

static ScoreboardRecord ValidScoreboardRecord(int track)
{
	ScoreboardRecord record = {0};
	record.timestamp = 1;
	for (int lap = 0; lap < LAPS_PER_RACE; lap++)
		record.lapTimes[lap] = 60.0f + lap;
	record.trackNum = track;
	record.difficulty = DIFFICULTY_MEDIUM;
	record.gameMode = GAME_MODE_PRACTICE;
	record.vehicleType = CAR_TYPE_MAMMOTH;
	record.place = 0;
	record.sex = 0;
	record.skin = CAVEMAN_SKIN_BROWN;
	return record;
}

static void TestScoreboardSanitization(void)
{
	Scoreboard scoreboard = {0};
	scoreboard.records[0][0] = ValidScoreboardRecord(0);
	assert(!SanitizeScoreboard(&scoreboard));

	ScoreboardRecord validSecond = ValidScoreboardRecord(0);
	validSecond.timestamp = 2;
	scoreboard.records[0][0].lapTimes[1] = NAN;
	scoreboard.records[0][1] = validSecond;
	assert(SanitizeScoreboard(&scoreboard));
	assert(scoreboard.records[0][0].timestamp == 2);
	assert(scoreboard.records[0][1].timestamp == 0);

	scoreboard = (Scoreboard){0};
	scoreboard.records[0][0] = ValidScoreboardRecord(0);
	scoreboard.records[0][0].trackNum = NUM_TRACKS;
	assert(SanitizeScoreboard(&scoreboard));
	assert(scoreboard.records[0][0].timestamp == 0);

	scoreboard = (Scoreboard){0};
	scoreboard.records[TRACK_NUM_ATLANTIS][0] = ValidScoreboardRecord(TRACK_NUM_ATLANTIS);
	scoreboard.records[TRACK_NUM_ATLANTIS][0].vehicleType = CAR_TYPE_SUB;
	assert(!SanitizeScoreboard(&scoreboard));
}

int main(void)
{
	TestEnvelopeValidation();
	TestCharacterValidation();
	TestConfigValidation();
	TestSyncMaskValidation();
	TestControlValidation();
	TestHostControlValidation();
	TestDeterministicEventMath();
	TestCursorClamping();
	TestPlaceholderFormatting();
	TestPrefsSanitization();
	TestScoreboardSanitization();
	puts("Validation tests passed.");
	return 0;
}
