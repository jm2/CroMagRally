#pragma once

#if _WIN32
#include <winsock2.h>
typedef SOCKET sockfd_t;
#else
typedef int sockfd_t;
#define INVALID_SOCKET -1
#endif

#define MAX_CLIENTS 6
#define kNSpCMRProtocol4CC 'CMR1'
#define kNSpPlayerNameLength 32

typedef enum
{
	// Send message to all connected players
	kNSpAllPlayers				= 0,

	// Used to send a message to the NSp game's host
	kNSpHostID					= 1,

	kNSpClientID0				= 2,

	// For use in a headless server setup
	kNSpHostOnly				= -1,

	kNSpUnspecifiedEndpoint		= -2,
}NSpPlayerSpecials;

enum
{
	kNSpError                   = -1,
	kNSpSystemMessagePrefix     = (int32_t)0x80000000,
	kNSpJoinRequest             = kNSpSystemMessagePrefix | 0x00000001,
	kNSpJoinApproved            = kNSpSystemMessagePrefix | 0x00000002,
	kNSpJoinDenied              = kNSpSystemMessagePrefix | 0x00000003,
	kNSpPlayerJoined            = kNSpSystemMessagePrefix | 0x00000004,
	kNSpPlayerLeft              = kNSpSystemMessagePrefix | 0x00000005,
	kNSpHostChanged             = kNSpSystemMessagePrefix | 0x00000006,
	kNSpGameTerminated          = kNSpSystemMessagePrefix | 0x00000007,
	kNSpGroupCreated            = kNSpSystemMessagePrefix | 0x00000008,
	kNSpGroupDeleted            = kNSpSystemMessagePrefix | 0x00000009,
	kNSpPlayerAddedToGroup      = kNSpSystemMessagePrefix | 0x0000000A,
	kNSpPlayerRemovedFromGroup  = kNSpSystemMessagePrefix | 0x0000000B,
	kNSpPlayerTypeChanged       = kNSpSystemMessagePrefix | 0x0000000C,
};

enum
{
	kNSpSendFlag_Junk			= 0x00100000,		// will be sent (try once) when there is nothing else pending
	kNSpSendFlag_Normal			= 0x00200000,		// will be sent immediately (try once)
	kNSpSendFlag_Registered		= 0x00400000		// will be sent immediately (guaranteed, in order)
};

enum
{
	kNSpGameFlag_DontAdvertise = 0x00000001,
	kNSpGameFlag_ForceTerminateGame = 0x00000002
};

enum
{
	kNSpRC_OK					= 0,
	kNSpRC_Failed				= -1,
	kNSpRC_SendFailed			= -2,
	kNSpRC_InvalidClient		= -3,
	kNSpRC_InvalidSocket		= -4,
	kNSpRC_NoGame				= -5,
};

typedef int32_t						NSpPlayerID;

typedef struct sockaddr_in*			NSpAddressReference;

typedef struct NSpCMRGame*			NSpGameReference;

typedef struct
{
	uint32_t						version;		// NetSprocket version cookie
	int32_t							what;			// The kind of message (e.g. player joined)
	NSpPlayerID						from;			// ID of the sender
	NSpPlayerID						to;				// (player or group) id of the intended recipient
	uint32_t						id;				// Unique ID for this message & (from) player
	uint32_t						when;			// Timestamp for the message
	uint32_t						messageLen;		// Bytes of data in the entire message (including the header)
} NSpMessageHeader;

typedef struct
{
	NSpMessageHeader				header;
	char							name[kNSpPlayerNameLength];
} NSpJoinRequestMessage;

typedef struct
{
	NSpMessageHeader 				header;
	char							reason[256];
} NSpJoinDeniedMessage;

typedef struct
{
	NSpMessageHeader 				header;
} NSpJoinApprovedMessage;

void NSpClearMessageHeader(NSpMessageHeader* h);

int NSpMessage_Send(NSpGameReference inGame, NSpMessageHeader* inMessage, int inFlags);

int NSpGame_Dispose(NSpGameReference inGame, int disposeFlags);

// The following functions aren't true NetSprocket calls.
bool NSpGame_IsValidClientID(NSpGameReference gameRef, NSpPlayerID id);
NSpPlayerID NSpGame_ClientSlotToID(NSpGameReference gameRef, int slot);
int NSpGame_ClientIDToSlot(NSpGameReference gameRef, NSpPlayerID id);
int NSpGame_AcceptNewClient(NSpGameReference gameRef);
int NSpGame_AckJoinRequest(NSpGameReference gameRef, NSpMessageHeader* inMessage);
int NSpGame_GetNumClients(NSpGameReference inGame);
