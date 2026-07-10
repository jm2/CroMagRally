#include "game.h"
#include "net_validation.h"
#include <math.h>

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
		&& message->numPlayers <= MAX_LOCAL_PLAYERS
		&& message->playerNum >= 0
		&& message->playerNum < message->numPlayers
		&& message->difficulty < NUM_DIFFICULTIES
		&& message->targetFPS <= MAX_GAME_FPS;
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
		&& message->refreshRate >= 0
		&& message->refreshRate <= 1000
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

Boolean NetValidateHostControlPayload(const NetHostControlInfoMessageType* message, int numRealPlayers)
{
	const uint32_t validControlBits = (1u << NUM_CONTROL_BITS) - 1u;
	const uint8_t validInputFlags = INPUT_FLAG_SUBSTITUTED | INPUT_FLAG_COALESCED;
	const float minHostFPS = 9.0f;
	const float maxHostFPS = MAX_GAME_FPS;
	const float maxAbsSyncCoord = 1000000.0f;
	const float maxAbsSyncRotation = 1000000.0f;

	if (!message || numRealPlayers < 1 || numRealPlayers > MAX_LOCAL_PLAYERS)
		return false;

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
		if ((event->type != kEvBecomeBot && event->type != kEvUnpauseForce)
			|| event->playerNum < 0
			|| event->playerNum >= numRealPlayers
			|| event->pad != 0
			|| event->effectiveFrame - message->frameCounter > NET_MAX_EVENT_LEAD)
		{
			return false;
		}

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
