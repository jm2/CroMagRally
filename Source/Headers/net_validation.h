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
Boolean NetValidateSyncPayload(NetInboundRole role, const NetSyncMessage* message);
Boolean NetValidatePlayerCharPayload(const NetPlayerCharTypeMessage* message, int expectedPlayer, int numRealPlayers);
Boolean NetValidateClientControlPayload(const NetClientControlInfoMessageType* message, int expectedPlayer, int numRealPlayers);
// numTotalPlayers: every car in the race. Become-bot and unpause events name a network
// player (below numRealPlayers); a CPU POW use may name any car but the host's.
Boolean NetValidateHostControlPayload(const NetHostControlInfoMessageType* message, int numRealPlayers, int numTotalPlayers);

// kEvCpuThrow's pad. Decoding rejects anything the encoder cannot produce; powType and
// backward may be NULL.
uint16_t NetEncodeCPUPOW(int powType, Boolean backward);
Boolean NetDecodeCPUPOW(uint16_t pad, short* powType, Boolean* backward);

// Unsupported monitor readings are unknown (0), not a minimum-FPS proposal.
int NetNormalizeRefreshRate(int refreshRate);

// Barrier masks are expressed in NSp player-ID space. Bits belonging to players who
// have since disconnected must not keep an otherwise-complete barrier wedged.
uint32_t NetRetainActiveSyncBits(uint32_t syncedMask, uint32_t activeMask);
Boolean NetAreAllActivePlayersSynced(uint32_t syncedMask, uint32_t activeMask);
