#pragma once

#include "network.h"

typedef enum NetInboundRole
{
	kNetInbound_Host,
	kNetInbound_Client,
} NetInboundRole;

uint32_t NetExpectedMessageLength(int32_t what);
Boolean NetValidateInboundEnvelope(NetInboundRole role, const NSpMessageHeader* message);
Boolean NetValidateConfigPayload(const NetConfigMessage* message);
Boolean NetValidatePlayerCharPayload(const NetPlayerCharTypeMessage* message, int expectedPlayer, int numRealPlayers);
Boolean NetValidateClientControlPayload(const NetClientControlInfoMessageType* message, int expectedPlayer, int numRealPlayers);
Boolean NetValidateHostControlPayload(const NetHostControlInfoMessageType* message, int numRealPlayers);

// Barrier masks are expressed in NSp player-ID space. Bits belonging to players who
// have since disconnected must not keep an otherwise-complete barrier wedged.
uint32_t NetRetainActiveSyncBits(uint32_t syncedMask, uint32_t activeMask);
Boolean NetAreAllActivePlayersSynced(uint32_t syncedMask, uint32_t activeMask);
