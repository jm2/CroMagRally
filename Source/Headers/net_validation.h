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
