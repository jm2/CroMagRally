#include "game.h"
#include "net_validation.h"
#include <math.h>

// The lobby hands every network player (host + clients) a gPlayerInfo slot, readiness and
// active-player masks hold one bit per NSp player ID, and every client may leave in the same
// host frame, each scheduling a become-bot event that must fit in one host control message.
_Static_assert(MAX_CLIENTS <= MAX_PLAYERS, "every network player needs a player slot");
_Static_assert(MAX_CLIENTS <= 32, "NSp player-ID masks are uint32_t");
_Static_assert(NET_MAX_PENDING_EVENTS >= MAX_CLIENTS - 1, "one pending become-bot event per client");
_Static_assert(NET_MAX_PENDING_EVENTS >= MAX_PLAYERS - 1, "one pending event per non-host player");
_Static_assert(MAX_POW_TYPES - 1 <= NET_CPU_POW_TYPE_MASK, "a CPU POW event names every POW type");

int NetNormalizeRefreshRate(int refreshRate)
{
	if (refreshRate < NET_MIN_FPS)
		return 0;
	return refreshRate > MAX_GAME_FPS ? MAX_GAME_FPS : refreshRate;
}

uint32_t NetExpectedMessageLength(int32_t what)
{
	switch (what)
	{
		case kNSpJoinRequest:				return sizeof(NSpJoinRequestMessage);
		case kNSpJoinApproved:				return sizeof(NSpJoinApprovedMessage);
		case kNSpJoinDenied:				return sizeof(NSpJoinDeniedMessage);
		case kNSpPlayerJoined:				return sizeof(NSpPlayerJoinedMessage);
		case kNSpPlayerLeft:				return sizeof(NSpPlayerLeftMessage);
		case kNSpGameTerminated:			return sizeof(NSpGameTerminatedMessage);
		case kNetConfigureMessage:			return sizeof(NetConfigMessage);
		case kNetPlayerCharTypeMessage:		return sizeof(NetPlayerCharTypeMessage);
		case kNetSyncMessage:				return sizeof(NetSyncMessage);
		case kNetHostControlInfoMessage:		return sizeof(NetHostControlInfoMessageType);
		case kNetClientControlInfoMessage:	return sizeof(NetClientControlInfoMessageType);
		case kNetKeepAliveMessage:			return sizeof(NSpMessageHeader);
		default:							return 0;
	}
}

Boolean NetValidateInboundEnvelope(NetInboundRole role, const NSpMessageHeader* message)
{
	uint32_t expectedLength = NetExpectedMessageLength(message->what);
	if (expectedLength == 0 || message->messageLen != expectedLength)
		return false;

	if (role == kNetInbound_Host)
	{
		switch (message->what)
		{
			case kNSpJoinRequest:
				return message->to == kNSpHostID;
			case kNetPlayerCharTypeMessage:
				return message->to == kNSpAllPlayers;
			case kNetSyncMessage:
			case kNetClientControlInfoMessage:
			case kNetKeepAliveMessage:
				return message->to == kNSpHostID;
			default:
				return false;
		}
	}

	switch (message->what)
	{
		case kNSpJoinApproved:
			return message->to > kNSpHostID
				&& message->to < kNSpHostID + MAX_CLIENTS;
		case kNSpPlayerJoined:
		case kNSpPlayerLeft:
		case kNetConfigureMessage:
			return message->to >= kNSpHostID;
		case kNSpJoinDenied:
			return message->to == kNSpUnspecifiedEndpoint || message->to >= kNSpHostID;
		case kNSpGameTerminated:
			return message->to == kNSpAllPlayers || message->to >= kNSpHostID;
		case kNetPlayerCharTypeMessage:
		case kNetSyncMessage:
		case kNetHostControlInfoMessage:
		case kNetKeepAliveMessage:
			return message->to == kNSpAllPlayers;
		default:
			return false;
	}
}

Boolean NetValidateConfigPayload(const NetConfigMessage* message)
{
	if (message->gameMode < GAME_MODE_MULTIPLAYERRACE
		|| message->gameMode > GAME_MODE_CAPTUREFLAG
		|| message->trackNum < 0
		|| message->trackNum >= NUM_TRACKS)
	{
		return false;
	}

	// Race tracks and battle arenas have different data contracts. In particular,
	// scoreboard storage only has NUM_RACE_TRACKS rows, so never accept a race that
	// names a battle arena. Conversely, battle modes require an arena.
	if (message->gameMode == GAME_MODE_MULTIPLAYERRACE)
	{
		if (message->trackNum >= NUM_RACE_TRACKS)
			return false;
	}
	else if (message->trackNum < NUM_RACE_TRACKS)
	{
		return false;
	}

	return message->age >= 0
		&& message->age < NUM_AGES
		&& message->numPlayers >= 1
		&& message->numPlayers <= MAX_CLIENTS						// network players, not this machine's split-screen players
		&& message->playerNum >= 0
		&& message->playerNum < message->numPlayers
		&& message->difficulty < NUM_DIFFICULTIES
		&& message->targetFPS >= NET_MIN_FPS
		&& message->targetFPS <= MAX_GAME_FPS
		&& message->cpuFill <= 1
		&& (!message->cpuFill || message->gameMode == GAME_MODE_MULTIPLAYERRACE);	// arenas have no AI paths
}

Boolean NetValidateSyncPayload(NetInboundRole role, const NetSyncMessage* message)
{
	if (!message || message->pad != 0)
		return false;

	// Client readiness does not propose a cap; only the host finalizes it.
	if (role == kNetInbound_Host)
		return message->targetFPS == 0;

	return message->targetFPS >= NET_MIN_FPS && message->targetFPS <= MAX_GAME_FPS;
}

uint32_t NetRetainActiveSyncBits(uint32_t syncedMask, uint32_t activeMask)
{
	return syncedMask & activeMask;
}

Boolean NetAreAllActivePlayersSynced(uint32_t syncedMask, uint32_t activeMask)
{
	return NetRetainActiveSyncBits(syncedMask, activeMask) == activeMask;
}

Boolean NetValidatePlayerCharPayload(const NetPlayerCharTypeMessage* message, int expectedPlayer, int numRealPlayers)
{
	return expectedPlayer >= 0
		&& message->playerNum == expectedPlayer
		&& message->playerNum < numRealPlayers
		&& message->vehicleType >= 0
		&& message->vehicleType < NUM_LAND_CAR_TYPES
		&& message->sex >= 0
		&& message->sex <= 1
		&& message->skin >= 0
		&& message->skin < NUM_CAVEMAN_SKINS
		&& (message->refreshRate == 0
			|| (message->refreshRate >= NET_MIN_FPS && message->refreshRate <= MAX_GAME_FPS))
		&& (message->connectionType == 0 || message->connectionType == 1);
}

Boolean NetValidateClientControlPayload(const NetClientControlInfoMessageType* message, int expectedPlayer, int numRealPlayers)
{
	const uint32_t validControlBits = (1u << NUM_CONTROL_BITS) - 1u;
	return expectedPlayer >= 0
		&& message->playerNum == expectedPlayer
		&& message->playerNum < numRealPlayers
		&& (message->pauseState == 0 || message->pauseState == 1)
		&& (message->controlBits & ~validControlBits) == 0
		&& isfinite(message->analogSteering.x)
		&& isfinite(message->analogSteering.y)
		&& message->analogSteering.x >= -1.0f
		&& message->analogSteering.x <= 1.0f
		&& message->analogSteering.y >= -1.0f
		&& message->analogSteering.y <= 1.0f;
}

uint16_t NetEncodeCPUPOW(int powType, Boolean backward)
{
	return (uint16_t) ((powType & NET_CPU_POW_TYPE_MASK) | (backward ? NET_CPU_POW_BACKWARD : 0));
}

Boolean NetDecodeCPUPOW(uint16_t pad, short* powType, Boolean* backward)
{
	if ((pad & ~(NET_CPU_POW_TYPE_MASK | NET_CPU_POW_BACKWARD)) != 0
		|| (pad & NET_CPU_POW_TYPE_MASK) >= MAX_POW_TYPES)
	{
		return false;
	}

	if (powType)
		*powType = (short) (pad & NET_CPU_POW_TYPE_MASK);
	if (backward)
		*backward = (pad & NET_CPU_POW_BACKWARD) != 0;
	return true;
}

Boolean NetValidateHostControlPayload(const NetHostControlInfoMessageType* message, int numRealPlayers, int numTotalPlayers)
{
	const uint32_t validControlBits = (1u << NUM_CONTROL_BITS) - 1u;
	const uint8_t validInputFlags = INPUT_FLAG_SUBSTITUTED | INPUT_FLAG_COALESCED;
	const float minHostFPS = NET_MIN_FPS;
	const float maxHostFPS = MAX_GAME_FPS;
	const float maxAbsSyncCoord = 1000000.0f;
	const float maxAbsSyncRotation = 1000000.0f;

	if (!message || numRealPlayers < 1 || numRealPlayers > MAX_CLIENTS
		|| numTotalPlayers < numRealPlayers || numTotalPlayers > MAX_PLAYERS)
	{
		return false;
	}

	if (!isfinite(message->fps)
		|| !isfinite(message->fpsFrac)
		|| message->fps < minHostFPS
		|| message->fps > maxHostFPS
		|| message->fpsFrac < 1.0f / maxHostFPS
		|| message->fpsFrac > 1.0f / minHostFPS
		|| fabsf(message->fps * message->fpsFrac - 1.0f) > 0.01f)
	{
		return false;
	}

	for (int i = 0; i < MAX_PLAYERS; i++)
	{
		if ((message->controlBits[i] & ~validControlBits) != 0
			|| (message->controlBitsNew[i] & ~validControlBits) != 0
			|| !isfinite(message->analogSteering[i].x)
			|| !isfinite(message->analogSteering[i].y)
			|| message->analogSteering[i].x < -1.0f
			|| message->analogSteering[i].x > 1.0f
			|| message->analogSteering[i].y < -1.0f
			|| message->analogSteering[i].y > 1.0f
			|| !isfinite(message->syncPos[i].x)
			|| !isfinite(message->syncPos[i].y)
			|| !isfinite(message->syncPos[i].z)
			|| fabsf(message->syncPos[i].x) > maxAbsSyncCoord
			|| fabsf(message->syncPos[i].y) > maxAbsSyncCoord
			|| fabsf(message->syncPos[i].z) > maxAbsSyncCoord
			|| !isfinite(message->syncRotY[i])
			|| fabsf(message->syncRotY[i]) > maxAbsSyncRotation
			|| (message->pauseState[i] != 0 && message->pauseState[i] != 1)
			|| (message->inputFlags[i] & ~validInputFlags) != 0
			|| message->queueDepth[i] >= NET_INPUT_QUEUE_SIZE
			|| message->targetDepth[i] > NET_MAX_INPUT_DEPTH)
		{
			return false;
		}
	}

	if (message->eventCount > NET_MAX_PENDING_EVENTS)
		return false;

	for (int i = 0; i < message->eventCount; i++)
	{
		const NetFrameEvent* event = &message->events[i];
		Boolean validEvent;
		switch (event->type)
		{
			case kEvBecomeBot:
			case kEvUnpauseForce:
				validEvent = event->playerNum >= 0 && event->playerNum < numRealPlayers && event->pad == 0;
				break;

			case kEvCpuThrow:												// the host's car is never a CPU
				validEvent = event->playerNum >= 1 && event->playerNum < numTotalPlayers
					&& NetDecodeCPUPOW(event->pad, NULL, NULL);
				break;

			default:
				validEvent = false;
				break;
		}

		if (!validEvent || event->effectiveFrame - message->frameCounter > NET_MAX_EVENT_LEAD)
			return false;

		// The host never has two events of one type pending for one player: it dedupes
		// leaves, and a CPU's next POW use waits until its last one applied. A player's
		// become-bot and its first POW use as a bot never share a packet either (the host
		// stops sending an event once its frame passed), but different types may.
		for (int j = 0; j < i; j++)
		{
			if (message->events[j].type == event->type
				&& message->events[j].playerNum == event->playerNum)
			{
				return false;
			}
		}
	}

	return true;
}
