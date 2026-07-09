//
// network.h
//

#pragma once

#include "main.h"
#include "netsprocket.h"


enum
{
	kNetConfigureMessage			= 'ncfg',
	kNetPlayerCharTypeMessage		= 'type',
	kNetSyncMessage					= 'sync',
	kNetHostControlInfoMessage		= 'hctl',
	kNetClientControlInfoMessage	= 'cctl',
	kNetKeepAliveMessage			= 'keep',		// CMR7 Stage 4: header-only heartbeat (radios awake + lastHeard fresh outside in-game streaming)
};

		/***************************/
		/* MESSAGE DATA STRUCTURES */
		/***************************/

		/* GAME CONFIGURATION MESSAGE */

typedef struct
{
	NSpMessageHeader	h;
	int8_t				gameMode;							// game mode (tag, race, etc.)
	int8_t				age;								// which age to play for race mode
	int8_t				trackNum;							// which track to play for battle modes
	int8_t				playerNum;							// this player's index
	int8_t				numPlayers;							// # players in net game
	//	uint8_t				numTracksCompleted;					// pass saved game value to clients so we're all the same here
	uint8_t				difficulty;							// pass host's difficulty setting so we're in sync
	uint8_t				tagDuration;						// # minutes in tag game
	uint8_t				reserved;							// CMR7: was useRedundancy (retired; per-client adaptive depth replaces it)
	uint16_t			targetFPS;							// The FPS cap for the game (min of all players)
}NetConfigMessage;
_Static_assert(sizeof(NetConfigMessage) <= kNSpMaxMessageLength, "config msg fits");

		/* SYNC MESSAGE */

typedef struct
{
	NSpMessageHeader	h;
	uint16_t			targetFPS;							// CMR7: host-finalized FPS (written/read in Stage 4)
	uint16_t			pad;
}NetSyncMessage;
_Static_assert(sizeof(NetSyncMessage) <= kNSpMaxMessageLength, "sync msg fits");


		/* FRAME-ALIGNED GAME EVENT (CMR7) */
		//
		// Carried in the host control stream so every machine applies a leave/bot-conversion
		// at the identical sim frame. Fields land in the CMR7 bump; populated in Stage 4.
		//

enum { kEvReserved = 0, kEvBecomeBot = 1, kEvUnpauseForce = 2 };
enum { INPUT_FLAG_SUBSTITUTED = 0x01, INPUT_FLAG_COALESCED = 0x02 };

typedef struct
{
	uint32_t			effectiveFrame;					// host sim frame at which every machine applies it
	uint8_t				type;							// kEv*
	int8_t				playerNum;
	uint16_t			pad;
}NetFrameEvent;
_Static_assert(sizeof(NetFrameEvent) == 8, "NetFrameEvent ABI");

// Max frame-aligned events buffered/applied concurrently (host pending ring + per-machine apply
// table). The wire MUST carry the same count: NetCheck/leave can schedule one become-bot per
// in-flight player in a SINGLE host frame, all sharing one effectiveFrame, so a smaller wire cap
// would silently drop the surplus and desync the host vs clients (seed/state FATAL).
#define NET_MAX_PENDING_EVENTS	8


		/* HOST CONTROL INFO MESSAGE (CMR7) */

typedef struct
{
	NSpMessageHeader	h;
	float				fps, fpsFrac;
	uint32_t			randomSeed;						// simply used for error checking (all machines should have same seed!)
	uint32_t			frameCounter;					// host frame (gHostSendCounter)
	uint32_t			simTick;						// gSimulationFrame (diagnostic)
	uint32_t			controlBits[MAX_PLAYERS];
	uint32_t			controlBitsNew[MAX_PLAYERS];	// HOST-DERIVED edges (clients apply verbatim)
	OGLVector2D			analogSteering[MAX_PLAYERS];
	OGLPoint3D			syncPos[MAX_PLAYERS];			// Authoritative position from Host (rubber-band feed)
	float				syncRotY[MAX_PLAYERS];			// Authoritative Y-Rotation (Heading)
	uint32_t			ackInputSeq[MAX_PLAYERS];		// last REAL input seq applied per player
	uint8_t				pauseState[MAX_PLAYERS];
	uint8_t				inputFlags[MAX_PLAYERS];		// bit0 substituted, bit1 coalesced (telemetry)
	uint8_t				queueDepth[MAX_PLAYERS];		// telemetry
	uint8_t				targetDepth[MAX_PLAYERS];		// telemetry
	uint8_t				eventCount;						// 0..NET_MAX_PENDING_EVENTS (0 until Stage 4)
	NetFrameEvent		events[NET_MAX_PENDING_EVENTS];	// every concurrently-pending event MUST fit here (no broadcast starvation)
}NetHostControlInfoMessageType;
_Static_assert(sizeof(NetHostControlInfoMessageType) <= kNSpMaxMessageLength, "host msg fits");


		/* CLIENT CONTROL INFO MESSAGE (CMR7) */

typedef struct
{
	NSpMessageHeader	h;
	int16_t				playerNum;
	uint8_t				pauseState;
	uint8_t				pad;
	uint32_t			inputSeq;						// monotonic, client-owned (was frameCounter)
	uint32_t			lastHostFrameSeen;				// RTT/diagnostics
	uint32_t			controlBits;
	OGLVector2D			analogSteering;
}NetClientControlInfoMessageType;
_Static_assert(sizeof(NetClientControlInfoMessageType) <= kNSpMaxMessageLength, "client msg fits");


		/* PLAYER CHAR TYPE MESSAGE */

typedef struct
{
	NSpMessageHeader	h;
	int16_t				playerNum;
	int16_t				vehicleType;
	int16_t				sex;				// 0 = male, 1 = female
	int16_t				skin;
	int16_t				refreshRate;						// Client's monitor refresh rate
	int16_t				connectionType;						// 0 = Wired, 1 = WiFi
}NetPlayerCharTypeMessage;
_Static_assert(sizeof(NetPlayerCharTypeMessage) <= kNSpMaxMessageLength, "char-type msg fits");


//===============================================================================

// CMR7 Stage 3: result of consuming one staged host control packet from the client ring.
typedef enum
{
	kHostConsume_Empty		= 0,			// ring empty
	kHostConsume_Dup		= 1,			// popped but the handler dropped it (old/dup) — no sim step
	kHostConsume_Applied	= 2,			// popped + applied (counter advanced) — caller steps the sim
} HostConsumeResult;


void InitNetworkManager(void);
void ShutdownNetworkManager(void);
Boolean SetupNetworkHosting(void);
Boolean SetupNetworkJoin(void);
void ClientTellHostLevelIsPrepared(void);
void HostWaitForPlayersToPrepareLevel(void);

void HostSend_ControlInfoToClients(void);
void ClientSend_ControlInfoToHost(void);
void Client_PumpHostPackets(void);				// CMR7 Stage 3: non-blocking drain of host control packets into the client ring
HostConsumeResult Client_ConsumeHostPacketFromRing(void);	// CMR7 Stage 3: pop one + apply via the verbatim handler
Boolean Client_IsHoldBadgeVisible(void);		// CMR7 Stage 3: subtle net badge after ~250ms of host-packet absence
void ResetClientHostRing(void);					// CMR7 Stage 3: clear the client host-packet ring between net games

void Host_PumpClientInputs(void);				// CMR7: non-blocking drain of client inputs into per-player queues
void Host_ConsumeClientInputs(void);			// CMR7: depth controller + substitution + coalesce + single-apply
void Host_InitInputControl(void);				// CMR7: seed per-client D_init before the game loop
void SampleAndSendLocalInput(Boolean* outSchedulePause);	// CMR7: wall-clock-paced client input sampler
void ResetNetGameTransientState(void);			// CMR7: zero all per-session host/client net state

void Net_Pump(void);
int Net_GetConnectionHint(void);				// CMR7: per-client D_init seed (1 = WiFi, 0 = wired)

void ApplyPendingFrameEvents(void);				// CMR7 Stage 4: apply frame-aligned events (become-bot) for the frame just simulated
void NetCheck_ConnectionTimeouts(void);			// CMR7 Stage 4: per-frame lastHeard badge/drop policy (host + client)
void Net_MaybeSendKeepAlive(void);				// CMR7 Stage 4: throttled header-only heartbeat (lobby/barriers keep radios awake)
void Net_RefreshLastHeard(void);				// CMR7 Stage 4: reset all liveness clocks to now (game-loop entry)
void SetNetworkPowerMode(Boolean enabled);		// Android network-session WiFi/multicast locks; no-op elsewhere

void PlayerBroadcastVehicleType(void);
Boolean GetVehicleSelectionFromNetPlayers(void);


void EndNetworkGame(void);

//===============================================================================

enum
{
	kNetSequence_Offline,

	kNetSequence_HostOffline = 100,
	kNetSequence_HostLobbyOpen,
	kNetSequence_HostReadyToStartGame,
	kNetSequence_HostStartingGame,

	kNetSequence_ClientOffline = 200,
	kNetSequence_ClientSearchingForGames,
	kNetSequence_ClientFoundGames,
	kNetSequence_ClientJoiningGame,
	kNetSequence_ClientJoinedGame,

	kNetSequence_WaitingForPlayerVehicles = 300,
	kNetSequence_GotAllPlayerVehicles,

	kNetSequence_HostWaitForPlayersToPrepareLevel = 400,
	kNetSequence_ClientWaitForSyncFromHost,

	kNetSequence_GameLoop = 500,

	// all messages below are used to report errors post-match
	kNetSequence_Error = 600,
	kNetSequence_ClientOfflineBecauseHostBailed,
	kNetSequence_ClientOfflineBecauseHostUnreachable,
	kNetSequence_ClientOfflineBecauseKicked,
	kNetSequence_OfflineEverybodyLeft,
	kNetSequence_SeedDesync,
	kNetSequence_PositionDesync,
	kNetSequence_ErrorSendFailed,
	kNetSequence_ErrorLostPacket,
	kNetSequence_ErrorNoResponseFromClients,
	kNetSequence_ErrorNoResponseFromHost,
	kNetSequence_ErrorProtocolViolation,
};


bool UpdateNetSequence(void);

Boolean IsNetGamePaused(void);
