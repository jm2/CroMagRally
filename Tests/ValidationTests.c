#include "game.h"
#include "net_validation.h"
#include "inputstate.h"
#include "lzss.h"
#include "localplayers.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

// Keep every check active in Release/RelWithDebInfo too; the standard assert macro disappears
// under NDEBUG, which would turn this executable into a false-positive test.
#define assert(condition) do { if (!(condition)) { \
	fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #condition); \
	exit(EXIT_FAILURE); \
} } while (0)

SuperTileStatus **gSuperTileStatusGrid;
long gNumSuperTilesDeep;
long gNumSuperTilesWide;
Boolean gNetGameInProgress;
short gMyNetworkPlayerNum;
short gNumLocalPlayers;

static void TestLZSSCapacity(void)
{
	// Ordinary literal and dictionary-encoded strings, with exact and larger
	// output buffers. Rejected operations must preserve the surrounding bytes.
	const unsigned char literals[] = {7, 'a', 'b', 'c'};
	const unsigned char repeat[] = {7, 'a', 'b', 'c', 0xee, 0xf3};
	unsigned char buffer[16];
	memset(buffer, 0xa5, sizeof(buffer));
	assert(LZSS_DecodeBuffer(literals, sizeof(literals), buffer + 1, 3) == 3);
	assert(memcmp(buffer + 1, "abc", 3) == 0 && buffer[0] == 0xa5 && buffer[4] == 0xa5);
	assert(LZSS_DecodeBuffer(repeat, sizeof(repeat), buffer + 1, 9) == 9);
	assert(memcmp(buffer + 1, "abcabcabc", 9) == 0 && buffer[10] == 0xa5);
	for (size_t capacity = 0; capacity < 9; capacity++)
	{
		memset(buffer, 0xa5, sizeof(buffer));
		assert(LZSS_DecodeBuffer(repeat, sizeof(repeat), buffer + 1, capacity) == -1);
		assert(buffer[0] == 0xa5 && buffer[capacity + 1] == 0xa5);
	}
	assert(LZSS_DecodeBuffer(repeat, sizeof(repeat) - 1, buffer, sizeof(buffer)) == -1);
	assert(LZSS_DecodeBuffer(literals, 1, buffer, sizeof(buffer)) == -1);
	assert(LZSS_DecodeBuffer(NULL, 0, NULL, 0) == 0);
	assert(LZSS_DecodeBuffer(NULL, 1, buffer, sizeof(buffer)) == -1);
}

static void TestBG3DMetadata(void)
{
	BG3DGeometryHeader geometry = {.numMaterials = 1, .numPoints = 3, .numTriangles = 1};
	assert(BG3D_ValidateGeometryHeader(&geometry, 1));
	geometry.numMaterials = MAX_MATERIAL_LAYERS + 1;
	assert(!BG3D_ValidateGeometryHeader(&geometry, 1));
	geometry.numMaterials = -1;
	assert(!BG3D_ValidateGeometryHeader(&geometry, 1));
	geometry.numMaterials = 1;
	geometry.layerMaterialNum[0] = 1;
	assert(!BG3D_ValidateGeometryHeader(&geometry, 1));
	geometry.layerMaterialNum[0] = 0;
	geometry.numPoints = UINT32_MAX;
	assert(!BG3D_ValidateGeometryHeader(&geometry, 1));
	geometry.numPoints = 3;
	geometry.numTriangles = UINT32_MAX;
	assert(!BG3D_ValidateGeometryHeader(&geometry, 1));

	BG3DTextureHeader texture = {.width = 3, .height = 2, .srcPixelFormat = GL_RGB,
		.dstPixelFormat = GL_RGB5_A1, .bufferSize = 18};
	assert(BG3D_ValidateTextureHeader(&texture));
	texture.bufferSize--;
	assert(!BG3D_ValidateTextureHeader(&texture));
	texture.bufferSize = 24;
	texture.srcPixelFormat = GL_RGBA;
	assert(BG3D_ValidateTextureHeader(&texture));
	texture.width = UINT32_MAX;
	assert(!BG3D_ValidateTextureHeader(&texture));
	texture.width = 0;
	assert(!BG3D_ValidateTextureHeader(&texture));
	texture.width = 3;
	texture.srcPixelFormat = -1;
	assert(!BG3D_ValidateTextureHeader(&texture));
}

static void TestInputStates(void)
{
	KeyState state = KEYSTATE_OFF;
	UpdateKeyState(&state, true);
	assert(state == KEYSTATE_PRESSED);
	assert(ResolveAnalogInput(state, true, false, 0) == 1.0f);
	UpdateKeyState(&state, true);
	assert(state == KEYSTATE_HELD);
	UpdateKeyState(&state, false);
	assert(state == KEYSTATE_UP);
	assert(ResolveAnalogInput(state, true, false, 0) == 0.0f);
	UpdateKeyState(&state, false);
	assert(state == KEYSTATE_OFF);

	state = KEYSTATE_IGNOREHELD;
	UpdateKeyState(&state, true);
	assert(state == KEYSTATE_IGNOREHELD);
	assert(ResolveAnalogInput(state, true, false, 0) == 0.0f);
	UpdateKeyState(&state, false);
	assert(state == KEYSTATE_OFF);
	UpdateKeyState(&state, true);
	assert(state == KEYSTATE_PRESSED);

	// Eligible keyboard input wins over a stick, but release/invalidation must
	// immediately expose the stick value without a spurious full-scale frame.
	assert(ResolveAnalogInput(state, true, true, 0.25f) == 1.0f);
	assert(ResolveAnalogInput(state, false, true, 0.25f) == 0.25f);
	assert(ResolveAnalogInput(state, false, false, 0.25f) == 0.0f);
	UpdateKeyState(&state, false);
	assert(ResolveAnalogInput(state, true, true, 0.25f) == 0.25f);
	assert(ResolveAnalogInput(KEYSTATE_IGNOREHELD, true, true, 0.5f) == 0.5f);
	assert(ResolveAnalogInput(KEYSTATE_OFF, true, true, 0.0f) == 0.0f);
}

static void TestLocalSlotMapping(void)
{
	// Split-screen: player i owns pane/gamepad slot i; CPU players have none.
	for (int numLocal = 1; numLocal <= MAX_LOCAL_PLAYERS; numLocal++)
	{
		for (int p = 0; p < MAX_PLAYERS; p++)
			assert(LocalSlotForPlayer(p, false, 0, numLocal) == (p < numLocal ? p : -1));
	}

	// Network: each machine's one human uses slot 0, whatever player number the host gave it.
	// Players 4 and 5 used to index the four-entry HUD, POW-row and gamepad arrays directly.
	for (int me = 0; me < MAX_PLAYERS; me++)
	{
		for (int p = 0; p < MAX_PLAYERS; p++)
			assert(LocalSlotForPlayer(p, true, me, 1) == (p == me ? 0 : -1));
	}

	assert(LocalSlotForPlayer(-1, false, 0, MAX_LOCAL_PLAYERS) == -1);
	assert(LocalSlotForPlayer(MAX_PLAYERS, true, MAX_PLAYERS, 1) == -1);
	assert(LocalSlotForPlayer(MAX_LOCAL_PLAYERS, false, 0, MAX_PLAYERS) == -1);

	// The session wrapper is the inverse of GetPlayerNum(pane).
	gNetGameInProgress = true;
	gNumLocalPlayers = 1;
	for (gMyNetworkPlayerNum = 0; gMyNetworkPlayerNum < MAX_PLAYERS; gMyNetworkPlayerNum++)
	{
		int slot = GetLocalSlotForPlayer(gMyNetworkPlayerNum);
		assert(slot == 0 && GetPlayerNum(slot) == gMyNetworkPlayerNum);
	}

	gNetGameInProgress = false;
	gMyNetworkPlayerNum = 0;
	for (gNumLocalPlayers = 1; gNumLocalPlayers <= MAX_LOCAL_PLAYERS; gNumLocalPlayers++)
	{
		for (int slot = 0; slot < gNumLocalPlayers; slot++)
			assert(GetLocalSlotForPlayer(GetPlayerNum(slot)) == slot);
	}
	gNumLocalPlayers = 1;
}

static void TestTerrainRenderResidency(void)
{
	SuperTileStatus row0[2] =
	{
		{.supertileIndex = 3, .statusFlags = SUPERTILE_IS_DEFINED, .playerHereFlags = 1},
		{.supertileIndex = 4, .statusFlags = 0, .playerHereFlags = 2},
	};
	SuperTileStatus row1[2] =
	{
		{.supertileIndex = 5, .statusFlags = SUPERTILE_IS_DEFINED | SUPERTILE_IS_USED_THIS_FRAME, .playerHereFlags = 4},
		{.supertileIndex = 6, .statusFlags = SUPERTILE_IS_USED_THIS_FRAME, .playerHereFlags = 8},
	};
	SuperTileStatus* grid[2] = {row0, row1};

	gSuperTileStatusGrid = grid;
	gNumSuperTilesDeep = 2;
	gNumSuperTilesWide = 2;

	KeepTerrainAliveForRender();

	assert(row0[0].statusFlags == (SUPERTILE_IS_DEFINED | SUPERTILE_IS_USED_THIS_FRAME));
	assert(row0[1].statusFlags == 0);
	assert(row1[0].statusFlags == (SUPERTILE_IS_DEFINED | SUPERTILE_IS_USED_THIS_FRAME));
	assert(row1[1].statusFlags == SUPERTILE_IS_USED_THIS_FRAME);
	assert(row0[0].supertileIndex == 3 && row0[0].playerHereFlags == 1);
	assert(row0[1].supertileIndex == 4 && row0[1].playerHereFlags == 2);
	assert(row1[0].supertileIndex == 5 && row1[0].playerHereFlags == 4);
	assert(row1[1].supertileIndex == 6 && row1[1].playerHereFlags == 8);
}

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

	message.numPlayers = MAX_CLIENTS;						// full lobby: host + MAX_CLIENTS-1 clients
	message.playerNum = MAX_CLIENTS - 1;
	assert(NetValidateConfigPayload(&message));
	message.playerNum = MAX_LOCAL_PLAYERS;					// more network players than split-screen panes
	assert(NetValidateConfigPayload(&message));
	message.numPlayers = MAX_CLIENTS + 1;
	message.playerNum = 1;
	assert(!NetValidateConfigPayload(&message));
	message.playerNum = MAX_CLIENTS;
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

	// CPU fill is 0 or 1, and 1 only for a race: arenas have no AI paths.
	message.cpuFill = 1;
	assert(!NetValidateConfigPayload(&message));
	for (int mode = GAME_MODE_MULTIPLAYERRACE; mode <= GAME_MODE_CAPTUREFLAG; mode++)
	{
		message.gameMode = mode;
		message.trackNum = mode == GAME_MODE_MULTIPLAYERRACE ? NUM_RACE_TRACKS - 1 : NUM_TRACKS - 1;
		for (int fill = 0; fill <= 255; fill++)
		{
			message.cpuFill = fill;
			assert(NetValidateConfigPayload(&message)
				== (fill == 0 || (fill == 1 && mode == GAME_MODE_MULTIPLAYERRACE)));
		}
	}
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

static void TestNetworkFPSValidation(void)
{
	const int rates[] = {-1, 0, 1, 8, 9, 60, 1000, 1001};
	const int advertisedRates[] = {0, 0, 0, 0, 9, 60, 1000, 1000};
	NetConfigMessage config = {0};
	config.gameMode = GAME_MODE_MULTIPLAYERRACE;
	config.numPlayers = 2;
	NetPlayerCharTypeMessage character = ValidCharMessage();
	NetSyncMessage sync = {0};

	for (size_t i = 0; i < sizeof(rates) / sizeof(rates[0]); i++)
	{
		int rate = rates[i];
		Boolean supported = rate >= NET_MIN_FPS && rate <= MAX_GAME_FPS;
		config.targetFPS = rate;
		character.refreshRate = rate;
		sync.targetFPS = rate;
		assert(NetValidateConfigPayload(&config) == supported);
		assert(NetValidatePlayerCharPayload(&character, 1, 2) == (supported || rate == 0));
		assert(NetValidateSyncPayload(kNetInbound_Client, &sync) == supported);
		assert(NetValidateSyncPayload(kNetInbound_Host, &sync) == (rate == 0));
		assert(NetNormalizeRefreshRate(rate) == advertisedRates[i]);
		character.refreshRate = NetNormalizeRefreshRate(rate);
		assert(NetValidatePlayerCharPayload(&character, 1, 2));
	}

	sync.targetFPS = 60;
	sync.pad = 1;
	assert(!NetValidateSyncPayload(kNetInbound_Client, &sync));
	sync.targetFPS = 0;
	assert(!NetValidateSyncPayload(kNetInbound_Host, &sync));
	assert(!NetValidateSyncPayload(kNetInbound_Host, NULL));
	assert(!NetValidateSyncPayload(kNetInbound_Client, NULL));
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
	assert(NetValidateHostControlPayload(&message, 4, 4));
	assert(!NetValidateHostControlPayload(NULL, 4, 4));
	assert(!NetValidateHostControlPayload(&message, 0, 0));
	assert(NetValidateHostControlPayload(&message, MAX_CLIENTS, MAX_CLIENTS));
	assert(!NetValidateHostControlPayload(&message, MAX_CLIENTS + 1, MAX_CLIENTS + 1));

	const float rates[] = {0, 1, 8, 9, 1000, 1001};
	for (size_t i = 0; i < sizeof(rates) / sizeof(rates[0]); i++)
	{
		message.fps = rates[i];
		message.fpsFrac = rates[i] > 0 ? 1.0f / rates[i] : 0;
		assert(NetValidateHostControlPayload(&message, 4, 4)
			== (rates[i] >= NET_MIN_FPS && rates[i] <= MAX_GAME_FPS));
	}

	message.fps = 9.0f;
	message.fpsFrac = 1.0f / 9.0f;
	assert(NetValidateHostControlPayload(&message, 4, 4));
	message.fps = 1000.0f;
	message.fpsFrac = 0.001f;
	assert(NetValidateHostControlPayload(&message, 4, 4));
	message = ValidHostControlMessage();
	message.fps = NAN;
	assert(!NetValidateHostControlPayload(&message, 4, 4));
	message = ValidHostControlMessage();
	message.fps = INFINITY;
	assert(!NetValidateHostControlPayload(&message, 4, 4));
	message = ValidHostControlMessage();
	message.fps = 8.99f;
	assert(!NetValidateHostControlPayload(&message, 4, 4));
	message = ValidHostControlMessage();
	message.fpsFrac = 0.0f;
	assert(!NetValidateHostControlPayload(&message, 4, 4));
	message = ValidHostControlMessage();
	message.fpsFrac = -INFINITY;
	assert(!NetValidateHostControlPayload(&message, 4, 4));
	message = ValidHostControlMessage();
	message.fpsFrac = 0.02f;
	assert(!NetValidateHostControlPayload(&message, 4, 4));

	message = ValidHostControlMessage();
	message.controlBits[MAX_PLAYERS - 1] = 1u << NUM_CONTROL_BITS;
	assert(!NetValidateHostControlPayload(&message, 4, 4));
	message = ValidHostControlMessage();
	message.controlBitsNew[0] = 1u << NUM_CONTROL_BITS;
	assert(!NetValidateHostControlPayload(&message, 4, 4));
	message = ValidHostControlMessage();
	message.controlBitsNew[0] = 1u << kControlBit_Forward;
	assert(NetValidateHostControlPayload(&message, 4, 4));

	message = ValidHostControlMessage();
	message.analogSteering[0] = (OGLVector2D){-1.0f, 1.0f};
	assert(NetValidateHostControlPayload(&message, 4, 4));
	message.analogSteering[0].x = -1.01f;
	assert(!NetValidateHostControlPayload(&message, 4, 4));
	message = ValidHostControlMessage();
	message.analogSteering[0].y = NAN;
	assert(!NetValidateHostControlPayload(&message, 4, 4));

	message = ValidHostControlMessage();
	message.syncPos[0] = (OGLPoint3D){-1000000.0f, 1000000.0f, 0.0f};
	message.syncRotY[0] = -1000000.0f;
	assert(NetValidateHostControlPayload(&message, 4, 4));
	message.syncPos[0].z = 1000001.0f;
	assert(!NetValidateHostControlPayload(&message, 4, 4));
	message = ValidHostControlMessage();
	message.syncPos[0].y = INFINITY;
	assert(!NetValidateHostControlPayload(&message, 4, 4));
	message = ValidHostControlMessage();
	message.syncRotY[0] = NAN;
	assert(!NetValidateHostControlPayload(&message, 4, 4));
	message = ValidHostControlMessage();
	message.syncRotY[0] = -1000001.0f;
	assert(!NetValidateHostControlPayload(&message, 4, 4));

	message = ValidHostControlMessage();
	message.pauseState[0] = 2;
	assert(!NetValidateHostControlPayload(&message, 4, 4));
	message = ValidHostControlMessage();
	message.inputFlags[0] = INPUT_FLAG_SUBSTITUTED | INPUT_FLAG_COALESCED;
	assert(NetValidateHostControlPayload(&message, 4, 4));
	message.inputFlags[0] |= 0x04;
	assert(!NetValidateHostControlPayload(&message, 4, 4));
	message = ValidHostControlMessage();
	message.queueDepth[0] = NET_INPUT_QUEUE_SIZE - 1;
	message.targetDepth[0] = NET_MAX_INPUT_DEPTH;
	assert(NetValidateHostControlPayload(&message, 4, 4));
	message.queueDepth[0] = NET_INPUT_QUEUE_SIZE;
	assert(!NetValidateHostControlPayload(&message, 4, 4));
	message = ValidHostControlMessage();
	message.targetDepth[0] = NET_MAX_INPUT_DEPTH + 1;
	assert(!NetValidateHostControlPayload(&message, 4, 4));

	message = ValidHostControlMessage();
	message.eventCount = 1;
	message.events[0] = (NetFrameEvent)
	{
		.effectiveFrame = message.frameCounter + NET_MAX_EVENT_LEAD,
		.type = kEvBecomeBot,
		.playerNum = 3,
	};
	assert(NetValidateHostControlPayload(&message, 4, 4));
	message.events[0].effectiveFrame++;
	assert(!NetValidateHostControlPayload(&message, 4, 4));
	message.events[0].effectiveFrame = message.frameCounter - 1;
	assert(!NetValidateHostControlPayload(&message, 4, 4));
	message.events[0].effectiveFrame = message.frameCounter;
	message.events[0].type = kEvReserved;
	assert(!NetValidateHostControlPayload(&message, 4, 4));
	message.events[0].type = kEvUnpauseForce;
	message.events[0].playerNum = -1;
	assert(!NetValidateHostControlPayload(&message, 4, 4));
	message.events[0].playerNum = 4;
	assert(!NetValidateHostControlPayload(&message, 4, 4));
	assert(NetValidateHostControlPayload(&message, MAX_CLIENTS, MAX_CLIENTS));
	message.events[0].playerNum = MAX_CLIENTS - 1;
	assert(NetValidateHostControlPayload(&message, MAX_CLIENTS, MAX_CLIENTS));
	message.events[0].playerNum = MAX_CLIENTS;
	assert(!NetValidateHostControlPayload(&message, MAX_CLIENTS, MAX_CLIENTS));
	message.events[0].playerNum = 0;
	message.events[0].pad = 1;
	assert(!NetValidateHostControlPayload(&message, 4, 4));

	message = ValidHostControlMessage();
	message.frameCounter = UINT32_MAX - 5;
	message.eventCount = 1;
	message.events[0] = (NetFrameEvent)
	{
		.effectiveFrame = 3,
		.type = kEvUnpauseForce,
		.playerNum = 0,
	};
	assert(NetValidateHostControlPayload(&message, 4, 4));

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
	assert(!NetValidateHostControlPayload(&message, 4, 4));

	// A full packet: one POW use per car but the host's, then leaves and unpauses.
	message = ValidHostControlMessage();
	message.eventCount = NET_MAX_PENDING_EVENTS;
	for (int i = 0; i < NET_MAX_PENDING_EVENTS; i++)
	{
		int other = i - (MAX_PLAYERS - 1);
		message.events[i] = (NetFrameEvent)
		{
			.effectiveFrame = message.frameCounter + NET_MAX_EVENT_LEAD,
			.type = other < 0 ? kEvCpuThrow : other < 4 ? kEvBecomeBot : kEvUnpauseForce,
			.playerNum = other < 0 ? i + 1 : other % 4,
			.pad = other < 0 ? NetEncodeCPUPOW(i % MAX_POW_TYPES, i & 1) : 0,
		};
	}
	assert(NetValidateHostControlPayload(&message, 4, MAX_PLAYERS));
	assert(!NetValidateHostControlPayload(&message, 4, MAX_PLAYERS - 1));	// names a car not in the race
	message.eventCount = NET_MAX_PENDING_EVENTS + 1;
	assert(!NetValidateHostControlPayload(&message, 4, MAX_PLAYERS));

	message = ValidHostControlMessage();
	message.events[0] = (NetFrameEvent){.effectiveFrame = UINT32_MAX, .type = 255, .playerNum = -1, .pad = 1};
	message.randomSeed = UINT32_MAX;
	message.simTick = UINT32_MAX;
	message.ackInputSeq[0] = UINT32_MAX;
	assert(NetValidateHostControlPayload(&message, 4, 4));
}

// kEvCpuThrow: which car uses which POW, in which direction.
static void TestCPUPOWEvents(void)
{
	for (int type = 0; type < MAX_POW_TYPES; type++)
	{
		for (int backward = 0; backward <= 1; backward++)
		{
			short decodedType = -1;
			Boolean decodedBackward = !backward;
			uint16_t pad = NetEncodeCPUPOW(type, backward);
			assert(NetDecodeCPUPOW(pad, &decodedType, &decodedBackward));
			assert(decodedType == type && decodedBackward == backward);
			assert(NetDecodeCPUPOW(pad, NULL, NULL));
		}
	}
	int valid = 0;
	for (uint32_t pad = 0; pad <= UINT16_MAX; pad++)
		valid += NetDecodeCPUPOW((uint16_t) pad, NULL, NULL);
	assert(valid == 2 * MAX_POW_TYPES);									// nothing the encoder can't produce
	assert(!NetDecodeCPUPOW(MAX_POW_TYPES, NULL, NULL));
	assert(!NetDecodeCPUPOW(NET_CPU_POW_TYPE_MASK, NULL, NULL));
	assert(!NetDecodeCPUPOW(0x20, NULL, NULL));

	NetHostControlInfoMessageType message = ValidHostControlMessage();
	message.eventCount = 1;
	message.events[0] = (NetFrameEvent)
	{
		.effectiveFrame = message.frameCounter + NET_MAX_EVENT_LEAD,
		.type = kEvCpuThrow,
		.playerNum = MAX_PLAYERS - 1,											// a fill CPU
		.pad = NetEncodeCPUPOW(POW_TYPE_MINE, true),
	};
	assert(NetValidateHostControlPayload(&message, 2, MAX_PLAYERS));
	assert(!NetValidateHostControlPayload(&message, 2, MAX_PLAYERS - 1));
	assert(!NetValidateHostControlPayload(&message, 2, 1));					// fewer cars than players
	assert(!NetValidateHostControlPayload(&message, 2, MAX_PLAYERS + 1));
	message.events[0].playerNum = 1;											// a replacement bot, no fill
	assert(NetValidateHostControlPayload(&message, 2, 2));
	message.events[0].playerNum = 0;											// the host's car is never a CPU
	assert(!NetValidateHostControlPayload(&message, 2, MAX_PLAYERS));
	message.events[0].playerNum = -1;
	assert(!NetValidateHostControlPayload(&message, 2, MAX_PLAYERS));
	message.events[0].playerNum = 1;
	message.events[0].pad = MAX_POW_TYPES;
	assert(!NetValidateHostControlPayload(&message, 2, MAX_PLAYERS));
	message.events[0].pad = NetEncodeCPUPOW(POW_TYPE_NITRO, false);
	assert(NetValidateHostControlPayload(&message, 2, MAX_PLAYERS));
	message.events[0].effectiveFrame++;
	assert(!NetValidateHostControlPayload(&message, 2, MAX_PLAYERS));

	// Becoming a bot and a POW use may share a packet; two uses by one car may not.
	message.events[0].effectiveFrame = message.frameCounter + 1;
	message.eventCount = 2;
	message.events[1] = message.events[0];
	message.events[1].type = kEvBecomeBot;
	message.events[1].pad = 0;
	assert(NetValidateHostControlPayload(&message, 2, MAX_PLAYERS));
	message.events[1].type = kEvCpuThrow;
	message.events[1].pad = NetEncodeCPUPOW(POW_TYPE_BONE, false);
	message.events[1].effectiveFrame++;
	assert(!NetValidateHostControlPayload(&message, 2, MAX_PLAYERS));
	message.events[1].playerNum = 2;
	assert(NetValidateHostControlPayload(&message, 2, MAX_PLAYERS));
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
	prefs.cpuFill = true;
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
	CHECK_REPAIR(cpuFill, 2);

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

static void TestScoreboardPlaceBound(void)
{
	// Places are checked against the fixed SCOREBOARD_MAX_PLACES, so builds with different
	// player limits keep each other's records (a 6-player build keeps a 12-player build's
	// 7th-12th places). A place past the format bound is discarded, not clamped, and the
	// next record moves up.
	for (int place = 0; place < SCOREBOARD_MAX_PLACES + 2; place++)
	{
		Scoreboard scoreboard = {0};
		ScoreboardRecord next = ValidScoreboardRecord(0);
		next.timestamp = 2;
		scoreboard.records[0][0] = ValidScoreboardRecord(0);
		scoreboard.records[0][0].place = (Byte) place;
		scoreboard.records[0][1] = next;

		const Boolean kept = place < SCOREBOARD_MAX_PLACES;
		assert(SanitizeScoreboard(&scoreboard) == !kept);
		assert(scoreboard.records[0][0].timestamp == (kept ? 1 : 2));
		assert(scoreboard.records[0][0].place == (kept ? place : 0));
		assert(scoreboard.records[0][1].timestamp == (kept ? 2 : 0));
	}
	assert(MAX_PLAYERS <= SCOREBOARD_MAX_PLACES && 12 <= SCOREBOARD_MAX_PLACES);
}

static void TestBoneNormalCoverage(void)
{
	DecomposedPointType points[2] = {
		{.numRefs = 2, .whichNormal = {2, 0}},
		{.numRefs = 2, .whichNormal = {1, 2}},
	};
	SkeletonDefType skeleton = {.numDecomposedPoints = 2, .numDecomposedNormals = 3,
		.decomposedPointList = points};
	uint16_t attachedPoints[] = {1, 0, 1};
	uint16_t normals[] = {799, 799, 799, 0xBEEF};
	BoneDefinitionType bone = {.numPointsAttachedToBone = 3, .pointList = attachedPoints, .normalList = normals};
	assert(BuildBoneNormalList(&skeleton, &bone) == 3);
	assert(normals[0] == 1 && normals[1] == 2 && normals[2] == 0 && normals[3] == 0xBEEF);
	bone.numPointsAttachedToBone = 1;
	attachedPoints[0] = 0;
	assert(BuildBoneNormalList(&skeleton, &bone) == 2);
	assert(normals[0] == 2 && normals[1] == 0); // coverage belongs to this bone, not another bone
	attachedPoints[0] = 2;
	assert(BuildBoneNormalList(&skeleton, &bone) < 0);
	attachedPoints[0] = 0;
	points[0].whichNormal[0] = -1;
	assert(BuildBoneNormalList(&skeleton, &bone) < 0);
	points[0].whichNormal[0] = 3;
	assert(BuildBoneNormalList(&skeleton, &bone) < 0);
}

int main(void)
{
	TestLZSSCapacity();
	TestBG3DMetadata();
	TestBoneNormalCoverage();
	TestInputStates();
	TestLocalSlotMapping();
	TestTerrainRenderResidency();
	TestEnvelopeValidation();
	TestCharacterValidation();
	TestConfigValidation();
	TestNetworkFPSValidation();
	TestSyncMaskValidation();
	TestControlValidation();
	TestHostControlValidation();
	TestCPUPOWEvents();
	TestDeterministicEventMath();
	TestCursorClamping();
	TestPlaceholderFormatting();
	TestPrefsSanitization();
	TestScoreboardSanitization();
	TestScoreboardPlaceBound();
	puts("Validation tests passed.");
	return 0;
}
