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
	return message->gameMode >= GAME_MODE_MULTIPLAYERRACE
		&& message->gameMode <= GAME_MODE_CAPTUREFLAG
		&& message->age >= 0
		&& message->age < NUM_AGES
		&& message->trackNum >= 0
		&& message->trackNum < NUM_TRACKS
		&& message->numPlayers >= 1
		&& message->numPlayers <= MAX_LOCAL_PLAYERS
		&& message->playerNum >= 0
		&& message->playerNum < message->numPlayers
		&& message->difficulty < NUM_DIFFICULTIES
		&& message->targetFPS <= 1000;
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
