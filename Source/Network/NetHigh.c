/****************************/
/* HIGH-LEVEL NETWORKING    */
/* By Brian Greenstone      */
/* (c)2000 Pangea Software  */
/* (c)2022 Iliyas Jorio     */
/****************************/


/***************/
/* EXTERNALS   */
/***************/

#include "game.h"
#include "network.h"
#include "net_validation.h"
#include "miscscreens.h"
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <SDL3/SDL.h>

#if defined(__APPLE__) && !defined(__IOS__) && !defined(__TVOS__)
#include <SystemConfiguration/SystemConfiguration.h>
#elif defined(_WIN32)
#include <windows.h>
#include <wlanapi.h>
#pragma comment(lib, "wlanapi.lib")
#endif

/**********************/
/*     PROTOTYPES     */
/**********************/

static OSErr HostSendGameConfigInfo(void);
static Boolean HandleGameConfigMessage(NetConfigMessage *inMessage);
static Boolean HandleOtherNetMessage(NSpMessageHeader	*message);
static void PlayerUnexpectedlyLeavesGame(NSpPlayerLeftMessage *mess);
static int FindHumanByNSpPlayerID(NSpPlayerID playerID);
int Net_GetConnectionHint(void);

/****************************/

/*    CONSTANTS             */
/****************************/

#define LOADING_TIMEOUT	15						// # seconds to wait for clients to load a level

// CMR7 Stage 4: frame-aligned events + connection-liveness policy.
#define NET_BADGE_MS			1000				// >1s silence from a peer -> connection badge (substitution continues)
#define NET_SILENCE_DROP_MS	5000				// deterministic clients cannot resume missed frames: drop after a short stall
#define NET_KEEPALIVE_MS		50					// 20Hz heartbeat throttle (lobby/barriers only; in-game streams 60pps)
#define NET_SEQUENCE_MESSAGE_BUDGET 32			// drain bursts without starving rendering/event processing

/**********************/
/*     VARIABLES      */
/**********************/

static int	gNumGatheredPlayers = 0;			// this is only used during gathering, gNumRealPlayers should be used during game!

int			gNetSequenceState = kNetSequence_Offline;

Boolean		gNetSprocketInitialized = false;

Boolean		gIsNetworkHost = false;
Boolean		gIsNetworkClient = false;
Boolean		gNetGameInProgress = false;

NSpGameReference	gNetGame = nil;
NSpSearchReference	gNetSearch = nil;

Str32			gPlayerNameStrings[MAX_PLAYERS];

uint32_t			gClientSendCounter[MAX_PLAYERS];
uint32_t			gHostSendCounter;

static char			gNetJoinDeniedReason[sizeof(((NSpJoinDeniedMessage*) 0)->reason)] =
	"THE HOST DENIED THE JOIN REQUEST.";

NetHostControlInfoMessageType	gHostOutMess;
NetClientControlInfoMessageType	gClientOutMess;


#pragma mark - Net pump

/********** NET PUMP **********/
//
// Stage 1: drain the per-socket non-blocking send rings. Called at frame start (Main.c
// PlayArea loop) and once per iteration inside the still-blocking Stage-1 receive loops so
// a backlogged uplink/broadcast keeps moving while the main thread waits — this is what
// prevents a host<->client mutual-wait deadlock. No-op outside a net game.
//

void Net_Pump(void)
{
	if (!gNetGameInProgress || gNetGame == nil)
		return;

	NSpGame_FlushSends(gNetGame);			// Stage 1: drain non-blocking send rings

	if (gIsNetworkHost)
		Host_PumpClientInputs();			// CMR7: non-blocking drain of client inputs into per-player queues
	else if (gIsNetworkClient)
		Client_PumpHostPackets();			// CMR7 Stage 3: non-blocking drain of host control packets into the ring
}


#pragma mark - Net fatal error


/********* NET FATAL ERROR **********/
//
// In release builds, this will throw us out of the game
// and cause an error message to appear in NetGather.
//

static void NetGameFatalError(int error)
{
#if _DEBUG
	DoFatalAlert("NetGameFatalError %d", error);
#else
	EndNetworkGame();
	gGameOver = true;
	gNetSequenceState = error;
#endif
}


#pragma mark - Player sync mask

uint32_t gPlayerSyncMask;

static void ClearPlayerSyncMask(void)
{
	gPlayerSyncMask = 0;

}

// Bounds check for a player index pulled off the wire, before it is used to index
// gPlayerInfo[] / sHostInputQueues[] / gClientSendCounter[] (all sized MAX_PLAYERS).
static inline Boolean IsValidPlayerNum(int n)
{
	return (n >= 0) && (n < MAX_PLAYERS);
}

static void MarkPlayerSynced(NSpPlayerID playerID)
{
	if (playerID < kNSpHostID || playerID >= kNSpHostID + MAX_CLIENTS)
		return;
	gPlayerSyncMask |= 1u << (uint32_t) playerID;
}

static void ForgetPlayerSync(NSpPlayerID playerID)
{
	if (playerID < kNSpHostID || playerID >= kNSpHostID + MAX_CLIENTS)
		return;
	gPlayerSyncMask &= ~(1u << (uint32_t) playerID);
}

// (PlayerIsSynced was only used by the deleted HostReceive busy-spin; the lobby/level-prep
// barriers use MarkPlayerSynced + AreAllPlayersSynced, so it is no longer needed.)

static Boolean AreAllPlayersSynced(void)

{
	uint32_t targetMask = NSpGame_GetActivePlayersIDMask(gNetGame);
	gPlayerSyncMask = NetRetainActiveSyncBits(gPlayerSyncMask, targetMask);
	return NetAreAllActivePlayersSynced(gPlayerSyncMask, targetMask);
}


#pragma mark - Host input control state (CMR7)

//
// CMR7 host-side input control. The host NEVER blocks on a client's radio: client input
// packets are drained (non-blocking) into per-client queues by Host_PumpClientInputs, then
// resolved each host frame by Host_ConsumeClientInputs, which SUBSTITUTES held input on an
// underrun and COALESCES a backlog down to the per-client adaptive depth D_i. Edge bits
// (controlBits_New) are derived host-side with the engine's own formula. This replaces the
// old blocking, 4-strike HostReceive busy-spin.
//

#define HOST_PACKET_BUDGET_PER_PUMP	64		// bound each pump; excess stays in TCP for a later pump
#define JITTER_WINDOW		128					// inter-arrival samples kept for the depth controller
#define GRACE_RETRIES		2					// bounded re-poll attempts (~1ms each) before substituting
#define SUB_DECAY_AFTER		30					// consecutive substituted frames before neutral decay
#define MAX_SAMPLE_BURST	3					// wall-clock sampler max packets emitted per call

typedef struct {
	int head;
	int tail;
	NetClientControlInfoMessageType msgs[NET_INPUT_QUEUE_SIZE];
} PacketQueue;

static PacketQueue sHostInputQueues[MAX_PLAYERS];

// Last REAL input applied per client: edge-derivation baseline + ack source. NOT decayed.
typedef struct {
	uint32_t	controlBits;		// last REAL bits (edge baseline; never touched by decay)
	OGLVector2D	analogSteering;		// last REAL analog (held while substituting)
	uint8_t		pauseState;			// last REAL pause (HELD on substitute -> never spurious unpause)
	uint32_t	lastAppliedSeq;		// == ackInputSeq broadcast; stale rule: seq <= this -> discard
	Boolean		valid;				// false until first real input applied this race
	uint16_t	subStreak;			// consecutive substituted frames
} HostClientInputState;
static HostClientInputState sLastApplied[MAX_PLAYERS];

// Per-client adaptive input-queue depth controller (sized from measured arrival jitter).
typedef struct {
	uint64_t	lastArrivalPC;			// SDL_GetPerformanceCounter of previous packet (0=none)
	float		deltas[JITTER_WINDOW];	// inter-arrival deltas (seconds)
	int			count, head;			// ring fill / write idx
	uint8_t		targetDepth;			// D_i, seeded D_init, clamp [1,8]
	float		decayAccumSec;			// accumulator for 1-frame-per-2s decay
	double		baselineSec;			// perf-time of last P95 recompute (throttle to <=4Hz)
	uint8_t		baselineDepth;			// live P95-derived baseline depth (decay floor)
} HostClientDepthState;
static HostClientDepthState sDepth[MAX_PLAYERS];

static uint8_t sInputFlags[MAX_PLAYERS];			// rebuilt each consume frame (telemetry)
static uint8_t sClientConnectionHint[MAX_PLAYERS];	// 0 wired / 1 wifi, from char-type msg

static double sNextSampleTime = 0.0;				// wall-clock sampler cursor (seconds, perf-counter base)


#pragma mark - Frame-aligned events (CMR7 Stage 4)

//
// CMR7 Stage 4 (G3 determinism fix): a player leaving / dropping is NOT converted to a bot at
// TCP-arrival time (which would draw ChooseTaggedPlayer's synced RNG at a stream position that
// differs across peers -> kNetSequence_SeedDesync FATAL). Instead the host schedules a
// kEvBecomeBot event at effectiveFrame = currentHostFrame + NET_MAX_EVENT_LEAD, re-broadcasts it
// in every host control packet until that frame, and EVERY machine (host included) applies it
// from a shared table at the IDENTICAL sim frame — right after the per-frame seed exchange and
// before MoveEverything — so the conditional RandomRange lands at the same RNG-stream position
// on all peers. The host keeps SUBSTITUTING the leaver's input between the leave and effectiveFrame.
//

// NET_MAX_PENDING_EVENTS is defined in network.h so the wire events[] array stays the same size.

// Host-only outgoing ring: events re-broadcast every frame until their effectiveFrame passes.
typedef struct { NetFrameEvent ev; Boolean active; } PendingEventSlot;
static PendingEventSlot sHostPendingEvents[NET_MAX_PENDING_EVENTS];

// Per-machine apply table (host + clients): dedupe by (effectiveFrame,type,playerNum) + apply once.
typedef struct { uint32_t effectiveFrame; uint8_t type; int8_t playerNum; Boolean applied; Boolean valid; } FrameEventEntry;
static FrameEventEntry sFrameEventTable[NET_MAX_PENDING_EVENTS];

// Connection-liveness badge (host: per-player slot; client: gNetBadge[0] = host link).
static Boolean		gNetBadge[MAX_PLAYERS];
static uint32_t		gLastNetSendMs = 0;			// keepalive throttle, bumped whenever we send anything


static void Queue_Push(int playerNum, NetClientControlInfoMessageType* msg)
{
	if (!IsValidPlayerNum(playerNum)) return;		// never index sHostInputQueues[] with an out-of-range wire value
	PacketQueue* q = &sHostInputQueues[playerNum];
	int next = (q->tail + 1) % NET_INPUT_QUEUE_SIZE;
	if (next != q->head)							// drop on full ring (>~2s backlog = deep underrun)
	{
		q->msgs[q->tail] = *msg;
		q->tail = next;
	}
}

static NetClientControlInfoMessageType* Queue_Peek(int playerNum)
{
	if (!IsValidPlayerNum(playerNum)) return NULL;
	PacketQueue* q = &sHostInputQueues[playerNum];
	if (q->head == q->tail) return NULL;
	return &q->msgs[q->head];
}

static void Queue_Pop(int playerNum)
{
	if (!IsValidPlayerNum(playerNum)) return;
	PacketQueue* q = &sHostInputQueues[playerNum];
	if (q->head != q->tail)
		q->head = (q->head + 1) % NET_INPUT_QUEUE_SIZE;
}

static int Queue_Count(int playerNum)
{
	if (!IsValidPlayerNum(playerNum)) return 0;
	PacketQueue* q = &sHostInputQueues[playerNum];
	int n = q->tail - q->head;
	if (n < 0) n += NET_INPUT_QUEUE_SIZE;
	return n;
}

// Engine edge-derivation formula (InputControlBits.c:145): newly-pressed bits this step.
static inline uint32_t Host_DeriveEdges(uint32_t prevBits, uint32_t newBits)
{
	return (newBits ^ prevBits) & newBits;
}

// Clear ALL per-process net state that must NOT survive from one net game to the next.
// Called from both setup paths and EndNetworkGame so stale state can never wedge a later
// game (queues holding future seqs, depth/substitution bookkeeping, the stall-strike
// counter, the send counters, and the wall-clock sampler cursor all used to persist).
void ResetNetGameTransientState(void)
{
	memset(sHostInputQueues, 0, sizeof(sHostInputQueues));
	memset(sLastApplied, 0, sizeof(sLastApplied));
	memset(sDepth, 0, sizeof(sDepth));
	memset(sInputFlags, 0, sizeof(sInputFlags));
	memset(sClientConnectionHint, 0, sizeof(sClientConnectionHint));
	gHostSendCounter = 0;
	memset(gClientSendCounter, 0, sizeof(gClientSendCounter));
	sNextSampleTime = 0.0;
	// CMR7 Stage 4: frame-aligned event + connection-liveness state (risk 11: a 2nd in-process net game must start clean).
	memset(sHostPendingEvents, 0, sizeof(sHostPendingEvents));
	memset(sFrameEventTable, 0, sizeof(sFrameEventTable));
	memset(gNetBadge, 0, sizeof(gNetBadge));
	gLastNetSendMs = 0;
	ResetClientHostRing();					// CMR7 Stage 3: empty the client host-packet ring + reset hold timers
}


#pragma mark -

/********* CONVERT NSPPLAYERID TO INTERNAL PLAYER NUM **********/

static int FindHumanByNSpPlayerID(NSpPlayerID playerID)
{
			/* FIND PLAYER NUM THAT MATCHES THE ID */

	for (int i = 0; i < gNumTotalPlayers; i++)
	{
		if (!gPlayerInfo[i].isComputer)							// skip computer players
		{
			if (gPlayerInfo[i].net.nspPlayerID == playerID)		// see if ID matches
				return i;
		}
	}

			/* NOT FOUND */

	return -1;
}

static Boolean ValidatePlayerCharMessage(const NetPlayerCharTypeMessage* mess)
{
	int expectedPlayer = FindHumanByNSpPlayerID(mess->h.from);
	return NetValidatePlayerCharPayload(mess, expectedPlayer, gNumRealPlayers);
}

static Boolean ValidateClientControlMessage(const NetClientControlInfoMessageType* mess)
{
	int expectedPlayer = FindHumanByNSpPlayerID(mess->h.from);
	return NetValidateClientControlPayload(mess, expectedPlayer, gNumRealPlayers);
}

static void RejectProtocolMessage(const NSpMessageHeader* message)
{
	printf("Protocol violation from player %d: %s\n", (int) message->from, NSp4CCString(message->what));
	if (gIsNetworkHost)
		NSpPlayer_DisconnectForProtocolViolation(gNetGame, message->from);
	else
		NetGameFatalError(kNetSequence_ErrorProtocolViolation);
}


#pragma mark - Frame-aligned events (CMR7 Stage 4)

// Record an event into the per-machine apply table, deduped by (effectiveFrame,type,playerNum).
// Called on the host (from Host_ScheduleFrameEvent) and on clients (from the host control handler,
// once per re-broadcast). Idempotent: a re-broadcast event already in the table is ignored.
static void RecordFrameEvent(uint32_t effectiveFrame, uint8_t type, int8_t playerNum)
{
	for (int k = 0; k < NET_MAX_PENDING_EVENTS; k++)			// dedupe
	{
		FrameEventEntry* e = &sFrameEventTable[k];
		if (e->valid && e->effectiveFrame == effectiveFrame && e->type == type && e->playerNum == playerNum)
			return;
	}

	for (int k = 0; k < NET_MAX_PENDING_EVENTS; k++)			// find a free / already-applied slot
	{
		FrameEventEntry* e = &sFrameEventTable[k];
		if (!e->valid || e->applied)
		{
			e->effectiveFrame	= effectiveFrame;
			e->type				= type;
			e->playerNum		= playerNum;
			e->applied			= false;
			e->valid			= true;
			return;
		}
	}
	// Table full of un-applied events (>8 concurrent leaves in one ~12-frame window): drop. Extremely
	// rare; the TCP keepalive backstop still converts the peer eventually via a later leave/drop.
}

// HOST: schedule a frame-aligned event. Deduped against any un-applied (type,playerNum) already in
// flight so a leave + a silence-timeout drop for the same player don't double-convert. effectiveFrame is the
// frame ABOUT to be sent (gHostSendCounter) + lead, so all clients receive it before applying.
static void Host_ScheduleFrameEvent(uint8_t type, int playerNum)
{
	if (!IsValidPlayerNum(playerNum))
		return;

	for (int k = 0; k < NET_MAX_PENDING_EVENTS; k++)			// dedupe an in-flight (un-applied) (type,playerNum)
	{
		FrameEventEntry* e = &sFrameEventTable[k];
		if (e->valid && !e->applied && e->type == type && e->playerNum == playerNum)
			return;
	}

	uint32_t effF = gHostSendCounter + NET_MAX_EVENT_LEAD;

	bool queuedForBroadcast = false;
	for (int s = 0; s < NET_MAX_PENDING_EVENTS; s++)			// push into the outgoing re-broadcast ring
	{
		if (!sHostPendingEvents[s].active)
		{
			sHostPendingEvents[s].ev.effectiveFrame	= effF;
			sHostPendingEvents[s].ev.type			= type;
			sHostPendingEvents[s].ev.playerNum		= (int8_t) playerNum;
			sHostPendingEvents[s].ev.pad			= 0;
			sHostPendingEvents[s].active			= true;
			queuedForBroadcast = true;
			break;
		}
	}

	// Only apply locally if we could also queue the event for broadcast. Applying it here while the
	// re-broadcast ring is full would convert the player to a bot on the host but never tell the
	// clients -> a one-sided state change and a guaranteed kNetSequence_SeedDesync. Dropping both
	// matches RecordFrameEvent's own drop-on-full contract; the TCP-keepalive backstop still
	// converts the peer via a later leave/drop.
	if (queuedForBroadcast)
		RecordFrameEvent(effF, type, (int8_t) playerNum);		// host applies via the same shared table as the clients
}

// HOST: map a leave message's NSpPlayerID to a dense player index and schedule its become-bot.
static void ScheduleBecomeBotFromLeave(NSpPlayerLeftMessage* mess)
{
	int i = FindHumanByNSpPlayerID(mess->playerID);
	if (i < 0)
	{
		printf("ScheduleBecomeBotFromLeave: no matching player id #%d; ignoring.\n", (int) mess->playerID);
		return;
	}
	Host_ScheduleFrameEvent(kEvBecomeBot, i);
}

// Convert gPlayerInfo[i] to a bot. This is the OLD PlayerUnexpectedlyLeavesGame body, now operating
// on a dense player index and invoked from ApplyPendingFrameEvents at the identical sim frame on every
// peer (so the tag-mode ChooseTaggedPlayer RNG draw is stream-aligned). Idempotent via the isComputer guard.
static void ApplyBecomeBot(int i)
{
	if (!IsValidPlayerNum(i))
		return;
	if (gPlayerInfo[i].isComputer)								// already a bot (dup event / leave+drop): nothing to do
		return;

	gPlayerInfo[i].isComputer = true;							// turn it into a computer player.
	gPlayerInfo[i].isEliminated = true;							// also eliminate from battles
	gPlayerInfo[i].net.pauseState = 0;							// unpause if they were paused
	gNumGatheredPlayers--;										// one less net player in the game
	// gNumRealPlayers--;										// DON'T decrement this, or the screen layout will change mid-game!

	// Use gNumGatheredPlayers (the live human count) here, NOT gNumRealPlayers (kept stable for the
	// split-screen layout). Both host and client decrement identically at this frame, so gGameOver fires
	// in lockstep.
	if (gNumGatheredPlayers <= 1)								// see if nobody to play with
	{
		gGameOver = true;
		if (gNetSequenceState < kNetSequence_Error)				// don't stomp a more specific post-match error
			gNetSequenceState = kNetSequence_OfflineEverybodyLeft;
	}

			/* HANDLE SPECIFICS */

	switch (gGameMode)
	{
		case	GAME_MODE_TAG1:
		case	GAME_MODE_TAG2:
				if (gPlayerInfo[i].isIt)
					ChooseTaggedPlayer();						// synced RNG draw — stream-aligned by the frame-aligned apply
				break;
	}
}

// HOST: drain the pending-event ring into an outgoing host control message. The wire events[] is sized
// NET_MAX_PENDING_EVENTS — the same as the pending ring and the per-machine apply table — so EVERY
// concurrently-pending event is broadcast each frame and no event is starved at the wire boundary.
// (NetCheck/leave can schedule several become-bots sharing one effectiveFrame in a single host frame;
// a 2-slot wire used to drop the surplus while the host still applied all of them -> seed/state desync.)
// An event is re-sent every frame INCLUDING its effectiveFrame packet (so the client records it before
// the matching consume), then expired the frame after (effectiveFrame < thisFrame).
static void Host_FillOutgoingEvents(NetHostControlInfoMessageType* m, uint32_t thisFrame)
{
	int n = 0;
	for (int s = 0; s < NET_MAX_PENDING_EVENTS; s++)
	{
		if (!sHostPendingEvents[s].active)
			continue;
		if (sHostPendingEvents[s].ev.effectiveFrame < thisFrame)	// already applied at its effectiveFrame: stop broadcasting
		{
			sHostPendingEvents[s].active = false;
			continue;
		}
		if (n < NET_MAX_PENDING_EVENTS)
			m->events[n++] = sHostPendingEvents[s].ev;
	}
	m->eventCount = (uint8_t) n;
}

// BOTH ROLES: apply every table entry whose effectiveFrame == the frame just simulated. Called from
// StepGameSimulation (and the pause callback) AFTER the per-frame seed exchange and BEFORE MoveEverything,
// so any conditional RNG draw inside ApplyBecomeBot lands at the identical stream position on all peers.
// host frame just sent / client frame just consumed both leave gHostSendCounter == thatFrame+1.
void ApplyPendingFrameEvents(void)
{
	if (!gNetGameInProgress || gHostSendCounter == 0)
		return;

	uint32_t frame = gHostSendCounter - 1;

	// Apply in a canonical order (playerNum asc, then type) so multi-event frames draw RNG in the same
	// order on every peer regardless of how the table was filled.
	for (;;)
	{
		int best = -1;
		for (int k = 0; k < NET_MAX_PENDING_EVENTS; k++)
		{
			FrameEventEntry* e = &sFrameEventTable[k];
			if (!e->valid || e->applied || e->effectiveFrame != frame)
				continue;
			if (best < 0
				|| e->playerNum < sFrameEventTable[best].playerNum
				|| (e->playerNum == sFrameEventTable[best].playerNum && e->type < sFrameEventTable[best].type))
				best = k;
		}
		if (best < 0)
			break;

		FrameEventEntry* e = &sFrameEventTable[best];
		switch (e->type)
		{
			case kEvBecomeBot:		ApplyBecomeBot(e->playerNum); break;
			case kEvUnpauseForce:	if (IsValidPlayerNum(e->playerNum)) gPlayerInfo[e->playerNum].net.pauseState = 0; break;
			default:				break;
		}
		e->applied = true;
	}
}


#pragma mark - Connection liveness (CMR7 Stage 4)

//
// CMR7 Stage 4: per-connection lastHeard policy, replacing the old session-killing 4-strike
// DATA_TIMEOUT. Called once/frame each role (main loop + pause). >1s silence raises a badge but
// substitution continues. A deterministic client cannot resume after missing seconds of host
// simulation, so sustained silence explicitly schedules a frame-aligned bot conversion and closes
// that peer instead of claiming a long mobile-background grace that finite send buffers cannot honor.
// Outbound-ring pressure may detect and drop an unread peer even sooner through the same leave path.
//
void NetCheck_ConnectionTimeouts(void)
{
	if (!gNetGameInProgress || gNetGame == nil)
		return;

	uint32_t now = (uint32_t) SDL_GetTicks();

	if (gIsNetworkHost)
	{
		int n = NSpGame_GetNumActivePlayers(gNetGame);
		for (int idx = 0; idx < n; idx++)
		{
			NSpPlayerID pid = NSpGame_GetNthActivePlayerID(gNetGame, idx);
			if (pid == kNSpHostID)							// skip the host's own slot
				continue;

			int pn = FindHumanByNSpPlayerID(pid);
			if (pn < 0)										// already converted to a bot / unknown id
				continue;

			uint32_t dt = now - NSpPlayer_GetLastHeard(gNetGame, pid);
			gNetBadge[pn] = (dt > NET_BADGE_MS);			// substitution keeps the sim alive regardless
			if (dt > NET_SILENCE_DROP_MS)
			{
				// Record the deterministic conversion before removing the low-level peer. Remaining
				// clients ignore the immediate PlayerLeft during gameplay and apply this event from
				// the ordered host-control stream at the same simulation frame as the host.
				Host_ScheduleFrameEvent(kEvBecomeBot, pn);
				NSpPlayer_Kick(gNetGame, pid);
				break;								// active-player indexing changed; resume next frame
			}
		}
	}
	else if (gIsNetworkClient)
	{
		uint32_t dt = now - NSpGame_GetHostLastHeard(gNetGame);
		gNetBadge[0] = (dt > NET_BADGE_MS);
		if (dt > NET_SILENCE_DROP_MS)
			NetGameFatalError(kNetSequence_ErrorNoResponseFromHost);
	}
}

//
// CMR7 Stage 4: throttled header-only heartbeat so WiFi radios never enter power-save and lastHeard
// stays fresh OUTSIDE in-game streaming (lobby, char-select, loading barriers). In-game needs none —
// the 60pps control/input streams keep both directions alive by construction.
//
void Net_MaybeSendKeepAlive(void)
{
	if (!gNetGameInProgress || gNetGame == nil)
		return;

	uint32_t now = (uint32_t) SDL_GetTicks();
	if (gLastNetSendMs != 0 && (now - gLastNetSendMs) < NET_KEEPALIVE_MS)
		return;
	gLastNetSendMs = now;

	NSpMessageHeader h;
	NSpClearMessageHeader(&h);
	h.what			= kNetKeepAliveMessage;
	h.to			= gIsNetworkHost ? kNSpAllPlayers : kNSpHostID;
	h.messageLen	= sizeof(h);
	NSpMessage_Send(gNetGame, &h, kNSpSendFlag_Registered);		// SendOrEnqueue: non-blocking
}

// CMR7 Stage 4: reset every per-connection liveness clock to now. Called at game-loop entry so the
// long lobby/vehicle-select/level-load gap can never trip a false drop on game-loop entry.
void Net_RefreshLastHeard(void)
{
	if (!gNetGameInProgress || gNetGame == nil)
		return;
	NSpGame_TouchAllLastHeard(gNetGame);
}


#pragma mark -


/******************* INIT NETWORK MANAGER *********************/
//
// Called once at boot
//

void InitNetworkManager(void)
{
#if _WIN32
	WSADATA wsaData;
	int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
	if (result != 0)
	{
		printf("WSAStartup failed: %d\n", result);
		return;
	}
#endif

	gNetSprocketInitialized = true;
}


/******************* SHuTDOWN NETWORK MANAGER *********************/

void ShutdownNetworkManager(void)
{
	if (gNetSprocketInitialized)
	{
#if _WIN32
		WSACleanup();
#endif
	}
}


/********************** END NETWORK GAME ******************************/
//
// Called from CleanupLevel() or when a player bails from game unexpectedly.
//

void EndNetworkGame(void)
{
	SetNetworkPowerMode(false);
	SetNetworkDiscoveryMode(false);

#if 0
	if ((!gNetGameInProgress) || (!gNetGame))								// only if we're running a net game
		return;
#endif

		/* THE HOST MUST TERMINATE IT ENTIRELY */

	if (gIsNetworkHost)
	{
		NSpGame_Dispose(gNetGame, kNSpGameFlag_ForceTerminateGame);	// do not renegotiate a new host
	}

			/* CLIENTS CAN JUST BAIL NORMALLY */
	else if (gIsNetworkClient)
	{
		NSpSearch_Dispose(gNetSearch);
		NSpGame_Dispose(gNetGame, 0);
	}


	gNetGameInProgress 	= false;
	gIsNetworkHost	= false;
	gIsNetworkClient	= false;
	gNetGame			= nil;
	gNetSearch			= nil;
	gNumGatheredPlayers	= 0;
	if (gNetSequenceState < kNetSequence_Error)
		gNetSequenceState	= kNetSequence_Offline;
	ClearPlayerSyncMask();
	gSimulationPaused	= false;
	ResetNetGameTransientState();			// clear host input queues, depth/substitution state, counters, stall-strike counter
}


#pragma mark - Net sequence

static void SetJoinDeniedReason(const NSpJoinDeniedMessage* denied)
{
	size_t out = 0;

	for (size_t in = 0;
		in < sizeof(denied->reason) && denied->reason[in] != '\0' && out + 1 < sizeof(gNetJoinDeniedReason);
		in++)
	{
		unsigned char c = (unsigned char) denied->reason[in];
		if (c >= 0x20 && c <= 0x7e)
			gNetJoinDeniedReason[out++] = (char) c;
	}

	gNetJoinDeniedReason[out] = '\0';
	if (out == 0)
		SDL_strlcpy(gNetJoinDeniedReason, "THE HOST DENIED THE JOIN REQUEST.", sizeof(gNetJoinDeniedReason));
}

const char* Net_GetJoinDeniedReason(void)
{
	return gNetJoinDeniedReason;
}

/****************** NETWORK SEQUENCE *********************/

static bool UpdateNetSequenceOnce(Boolean runFrameTicks)
{
	NSpMessageHeader* message = NULL;

	switch (gNetSequenceState)
	{
		case kNetSequence_HostLobbyOpen:
		{
			if (runFrameTicks && kNSpRC_OK != NSpGame_AdvertiseTick(gNetGame, gFramesPerSecondFrac))
			{
				gNetSequenceState = kNetSequence_Error;
				break;
			}

			if (runFrameTicks)
				NSpGame_AcceptNewClient(gNetGame);

			message = NSpMessage_Get(gNetGame);

			if (message) switch (message->what)
			{
				case kNSpJoinRequest:
				{
					// Acknowledge the request
					NSpGame_AckJoinRequest(gNetGame, message);
					break;
				}

				default:
					HandleOtherNetMessage(message);
					break;
			}

			break;
		}

		case kNetSequence_HostReadyToStartGame:
			if (!runFrameTicks)
				break;
			NSpGame_StopAdvertising(gNetGame);
			NSpGame_StopAcceptingNewClients(gNetGame);
			SetNetworkDiscoveryMode(false);
			if (noErr == HostSendGameConfigInfo())
			{
				gNetSequenceState = kNetSequence_HostStartingGame;
			}
			else
			{
				gNetSequenceState = kNetSequence_Error;
			}
			break;

		case kNetSequence_ClientSearchingForGames:
			if (!runFrameTicks)
				break;
			if (kNSpRC_OK != NSpSearch_Tick(gNetSearch))
			{
				gNetSequenceState = kNetSequence_Error;
			}
			else if (NSpSearch_GetNumGamesFound(gNetSearch) > 0)
			{
				gNetSequenceState = kNetSequence_ClientFoundGames;
			}
			break;

		case kNetSequence_ClientFoundGames:
			if (!runFrameTicks)
				break;
			if (kNSpRC_OK != NSpSearch_Tick(gNetSearch))
			{
				gNetSequenceState = kNetSequence_Error;
			}
			else if (NSpSearch_GetNumGamesFound(gNetSearch) == 0)
			{
				gNetSequenceState = kNetSequence_ClientSearchingForGames;
			}
			else
			{
				// Automatically join the first game we found

				gNetGame = NSpSearch_JoinGame(gNetSearch, 0);

				if (gNetGame)
				{
					gNetSequenceState = kNetSequence_ClientJoiningGame;
				}
				else
				{
					gNetSequenceState = kNetSequence_Error;
				}

				NSpSearch_Dispose(gNetSearch);
				gNetSearch = NULL;
				SetNetworkDiscoveryMode(false);
			}
			break;

		case kNetSequence_ClientJoiningGame:
		{
			message = NSpMessage_Get(gNetGame);

			if (message) switch (message->what)
			{
					case	kNetConfigureMessage:										// GOT GAME START INFO
						if (HandleGameConfigMessage((NetConfigMessage*) message))
							gNetSequenceState = kNetSequence_ClientJoinedGame;
						else
							RejectProtocolMessage(message);
						break;

//				case 	kNSpGameTerminated:											// Host terminated the game :(
//					break;

				case	kNSpJoinApproved:
				{
					NSpJoinApprovedMessage* approvedMessage = (NSpJoinApprovedMessage*) message;
					printf("Join approved! My player ID is %d\n", approvedMessage->header.to);
					break;
				}

				case	kNSpJoinDenied:
					SetJoinDeniedReason((const NSpJoinDeniedMessage*) message);
					gNetSequenceState = kNetSequence_ClientOfflineBecauseJoinDenied;
					EndNetworkGame();
					break;

				case	kNSpPlayerLeft:												// see if someone decided to un-join
//					ShowNamesOfJoinedPlayers();
					break;

				case	kNSpPlayerJoined:
//					ShowNamesOfJoinedPlayers();
					break;

				case	kNSpError:
					DoFatalAlert("kNetSequence_ClientJoiningGame: message == kNSpError");
					break;

				default:
					HandleOtherNetMessage(message);
					break;
			}

			break;
		}

		case kNetSequence_WaitingForPlayerVehicles:
		{
			if (AreAllPlayersSynced())
			{
				gNetSequenceState = kNetSequence_GotAllPlayerVehicles;
			}
			else
			{
				message = NSpMessage_Get(gNetGame);					// get message

				if (message) switch (message->what)
				{
					case	kNetPlayerCharTypeMessage:
					{
						NetPlayerCharTypeMessage *mess = (NetPlayerCharTypeMessage *) message;

						if (!ValidatePlayerCharMessage(mess))
						{
							RejectProtocolMessage(message);
							break;
						}

						if (gIsNetworkHost)
							NSpMessage_Send(gNetGame, message, kNSpSendFlag_Registered);

						gPlayerInfo[mess->playerNum].vehicleType = mess->vehicleType;	// save this player's type
						gPlayerInfo[mess->playerNum].sex = mess->sex;					// save this player's sex
						gPlayerInfo[mess->playerNum].skin = mess->skin;					// save this player's skin
						
						// CMR7 Stage 4: only the HOST computes the LCD framerate (min over the host + every
						// client char-type). Clients no longer lower it here — they adopt the host's
						// finalized value, broadcast once in the level-prep kNetSyncMessage, so the
						// negotiation is symmetric instead of each machine racing its own partial minimum.
						if (gIsNetworkHost && mess->refreshRate > 0 && mess->refreshRate < gTargetFPS)
						{
							gTargetFPS = mess->refreshRate;
							printf("New client joined with %dHz. Lowering TargetFPS to %d.\n", mess->refreshRate, gTargetFPS);
						}

						// CMR7: record this client's WiFi/wired hint as a per-client adaptive-depth seed
						// (replaces the old global gUseRedundancy all-or-nothing switch).
						sClientConnectionHint[mess->playerNum] = (mess->connectionType == 1) ? 1 : 0;
						if (mess->connectionType == 1)
							printf("New client %d is on WiFi. Seeding deeper input queue.\n", mess->playerNum);

						MarkPlayerSynced(message->from);								// inc count of received info

						break;
					}

					default:
						HandleOtherNetMessage(message);
				}
			}

			break;
		}


		case kNetSequence_HostWaitForPlayersToPrepareLevel:
		{
			message = NSpMessage_Get(gNetGame);					// get message
			if (message) switch (message->what)
			{
				case	kNetSyncMessage:
					MarkPlayerSynced(message->from);					// we got another player
					if (AreAllPlayersSynced())							// see if that's all of them
						gNetSequenceState = kNetSequence_GameLoop;
					break;

				default:
					HandleOtherNetMessage(message);
			}
			break;
		}


		case kNetSequence_ClientWaitForSyncFromHost:
		{
			message = NSpMessage_Get(gNetGame);

			if (message) switch (message->what)
			{
				case kNetSyncMessage:
					// CMR7 Stage 4: adopt the host-finalized LCD framerate before entering the game loop.
					if (message->messageLen >= sizeof(NetSyncMessage))
					{
						uint16_t fps = ((NetSyncMessage*) message)->targetFPS;
						if (fps > 0)
						{
							gTargetFPS = fps;
							printf("Adopted host-finalized TargetFPS: %d\n", gTargetFPS);
						}
					}
					gNetSequenceState = kNetSequence_GameLoop;
					puts("Got sync from host! Let's go!");
					break;

				case kNetPlayerCharTypeMessage:
					printf("!!!!!!! Got ANOTHER chartypemessage when I was waiting for sync!!\n");
					break;

				default:
					HandleOtherNetMessage(message);
					break;
			}
		}
	}

	if (message)
	{
		NSpMessage_Release(gNetGame, message);
		message = NULL;
		return true;
	}
	else
	{
		return false;
	}
}

bool UpdateNetSequence(void)
{
	bool gotAnyMessage = false;

	for (int i = 0; i < NET_SEQUENCE_MESSAGE_BUDGET; i++)
	{
		bool gotMessage = UpdateNetSequenceOnce(i == 0);
		if (!gotMessage)
			break;
		gotAnyMessage = true;
	}

	return gotAnyMessage;
}


#pragma mark - Host/Join


/****************** SETUP NETWORK HOSTING *********************/
//
// Called when this computer's user has selected to be a host for a net game.
//
// OUTPUT:  true == cancelled.
//
Boolean SetupNetworkHosting(void)
{
	ResetNetGameTransientState();			// start from a clean slate (belt-and-suspenders vs EndNetworkGame)
	SetNetworkDiscoveryMode(true);
	SetNetworkPowerMode(true);
	gNetSequenceState = kNetSequence_HostOffline;
	gTargetFPS = OGL_GetMonitorRefreshRate();
	sClientConnectionHint[0] = (Net_GetConnectionHint() == 1) ? 1 : 0;	// CMR7: host is always player 0; per-client D_init seed
	printf("Hosting Game. Local Refresh Rate: %dHz, WiFi: %d\n", gTargetFPS, sClientConnectionHint[0]);


			/* GET SOME NAMES */

/*
	CopyPString(gPlayerSaveData.playerName, gNetPlayerName);
	// TODO: this is probably STR_RACE + gGameMode - GAME_MODE_MULTIPLAYERRACE
	GetIndStringC(gameName, 1000 + gGamePrefs.language, 16 + (gGameMode - GAME_MODE_MULTIPLAYERRACE));	// name of game is game mode string
*/

			/* NEW HOST GAME */

	//status = NSpGame_Host(&gNetGame, theList, MAX_PLAYERS, gameName, password, gNetPlayerName, 0, kNSpClientServer, 0);
	gNetGame = NSpGame_Host();



	if (!gNetGame)
	{
		SetNetworkPowerMode(false);
		SetNetworkDiscoveryMode(false);
		gNetSequenceState = kNetSequence_Error;
		// Don't goto failure; show the error to the player
		DoNetGatherScreen();
		return true;
	}

	if (kNSpRC_OK != NSpGame_StartAdvertising(gNetGame))
	{
		SetNetworkPowerMode(false);
		SetNetworkDiscoveryMode(false);
		gNetSequenceState = kNetSequence_Error;
		DoNetGatherScreen();
		return true;
	}

	gNetSequenceState = kNetSequence_HostLobbyOpen;


			/* LET USERS JOIN IN */

	if (DoNetGatherScreen())
		goto failure;



	return false;


			/* SOMETHING WENT WRONG, SO BE GRACEFUL */

failure:
	NSpGame_Dispose(gNetGame, 0);
	SetNetworkPowerMode(false);
	SetNetworkDiscoveryMode(false);

	return true;
}


/*************** SETUP NETWORK JOIN ************************/
//
// OUTPUT:	false == let's go!
//			true = cancel
//

Boolean SetupNetworkJoin(void)
{
	ResetNetGameTransientState();			// start from a clean slate
	SDL_strlcpy(gNetJoinDeniedReason, "THE HOST DENIED THE JOIN REQUEST.", sizeof(gNetJoinDeniedReason));
	SetNetworkDiscoveryMode(true);
	SetNetworkPowerMode(true);

	gNetSequenceState = kNetSequence_ClientOffline;

	gNetSearch = NSpSearch_StartSearchingForGameHosts();

	if (gNetSearch)
	{
		gNetSequenceState = kNetSequence_ClientSearchingForGames;
	}
	else
	{
		gNetSequenceState = kNetSequence_Error;
	}

	Boolean cancelled = DoNetGatherScreen();
	if (cancelled)
	{
		SetNetworkPowerMode(false);
		SetNetworkDiscoveryMode(false);
	}
	return cancelled;
}




#pragma mark -

/*********************** SEND GAME CONFIGURATION INFO *******************************/
//
// Once everyone is in and we (the host) start things, then send this to all players to tell them we're on!
//

static OSErr HostSendGameConfigInfo(void)
{
OSStatus				status;
NetConfigMessage		message;

			/* GET PLAYER INFO */

	gNumRealPlayers = NSpGame_GetNumActivePlayers(gNetGame);

	gNumGatheredPlayers = gNumRealPlayers;							// live human count; decremented as players leave (drives the everybody-left check)

	gMyNetworkPlayerNum = 0;										// the host is always player #0


			/***********************************************/
			/* SEND GAME CONFIGURATION INFO TO ALL PLAYERS */
			/***********************************************/
			//
			// Send one message at a time to each individual client player with
			// specific info for each client.
			//

	int p = 1;														// start assigning player nums at 1 since Host is always #0

	for (int i = 0; i < gNumRealPlayers; i++)
	{
		NSpPlayerID clientID = NSpGame_GetNthActivePlayerID(gNetGame, i);

		gPlayerInfo[i].net.nspPlayerID = clientID;					// get NSp's playerID (for use when player leaves game)

		if (clientID != kNSpHostID)									// don't send start info to myself/host
		{
					/* MAKE NEW MESSAGE */

			NSpClearMessageHeader(&message.h);
			message.h.to 			= clientID;						// send to this client
			message.h.what 			= kNetConfigureMessage;			// set message type
			message.h.messageLen 	= sizeof(message);				// set size of message

			message.gameMode 		= gGameMode;					// set game Mode
			message.age		 		= gTheAge;						// set Age
			message.trackNum		= gTrackNum;					// set track #
			message.numPlayers 		= gNumRealPlayers;				// set # players
			message.playerNum 		= p++;							// set player #
//			message.numTracksCompleted= gTransientNumTracksCompleted;	// set # tracks completed (for car selection)
			message.difficulty		= gDifficulty;			// set difficulty
			message.tagDuration		= gTagDuration;					// set tag duration
			message.targetFPS		= gTargetFPS;					// Set the global target FPS
			message.reserved		= 0;							// CMR7: was useRedundancy (retired)

			status = NSpMessage_Send(gNetGame, &message.h, kNSpSendFlag_Registered);	// send message
			if (status)
			{
				DoAlert("HostSendGameConfigInfo: NSpMessage_Send failed!");
				break;
			}
		}
	}

			/************/
			/* CLEAN UP */
			/************/

	return(status);
}



/************************* HANDLE GAME CONFIGURATION MESSAGE *****************************/
//
// Called while polling in Client_WaitForGameConfigInfoDialogCallback.
//

static Boolean HandleGameConfigMessage(NetConfigMessage* inMessage)
{
	if (!NetValidateConfigPayload(inMessage))
		return false;

	gGameMode 			= inMessage->gameMode;
	gTheAge 			= inMessage->age;
	gTrackNum 			= inMessage->trackNum;
	gNumRealPlayers 	= inMessage->numPlayers;
	gMyNetworkPlayerNum = inMessage->playerNum;

	gNumGatheredPlayers = gNumRealPlayers;					// live human count; decremented as players leave (drives the everybody-left check)

	gDifficulty			= inMessage->difficulty;
	gTagDuration		= inMessage->tagDuration;
	gTargetFPS			= inMessage->targetFPS;

	printf("Join Config Received. TargetFPS: %d\n", gTargetFPS);


	// Copy transient settings
//	gTransientNumTracksCompleted = inMessage->numTracksCompleted;

	// Get NSp's playerIDs (for use when player leaves game)
	for (int i = 0; i < gNumRealPlayers; i++)
	{
		gPlayerInfo[i].net.nspPlayerID = NSpGame_GetNthActivePlayerID(gNetGame, i);
	}
	return true;
}


#pragma mark -


/********************* HOST WAIT FOR PLAYERS TO PREPARE LEVEL *******************************/
//
// Called right beofre PlayArea().  This waits for the sync message from the other client players
// indicating that they are ready to start playing.
//

void HostWaitForPlayersToPrepareLevel(void)
{
OSStatus				status;
NetSyncMessage			outMess;
int						startTick = TickCount();

		/********************************/
		/* WAIT FOR ALL CLIENTS TO SYNC */
		/********************************/

	ClearPlayerSyncMask();
	MarkPlayerSynced(NSpPlayer_GetMyID(gNetGame));			// we have our own info

	gNetSequenceState = kNetSequence_HostWaitForPlayersToPrepareLevel;

	while (gNetSequenceState == kNetSequence_HostWaitForPlayersToPrepareLevel)
	{
		bool gotMess = UpdateNetSequence();

		Net_MaybeSendKeepAlive();									// CMR7 Stage 4: heartbeat so radios stay awake + lastHeard stays fresh while loading

		if ((TickCount() - startTick) > (60 * LOADING_TIMEOUT))		// if no response for a while, then time out
		{
			NetGameFatalError(kNetSequence_ErrorNoResponseFromClients);
			return;
		}

		if (!gotMess && gNetSequenceState != kNetSequence_GameLoop)
		{
			SDL_Delay(100);
		}
	}

	if (gNetSequenceState != kNetSequence_GameLoop)
	{
		// Something went wrong
		return;
	}

	puts("---GOT SYNC FROM ALL PLAYERS---");

		/*******************************/
		/* TELL ALL CIENTS WE'RE READY */
		/*******************************/

	NSpClearMessageHeader(&outMess.h);
	outMess.h.to 			= kNSpAllPlayers;						// send to all clients
	outMess.h.what 			= kNetSyncMessage;						// set message type
	outMess.h.messageLen 	= sizeof(outMess);						// set size of message
	outMess.targetFPS		= gTargetFPS;							// CMR7 Stage 4: broadcast the finalized LCD framerate (host min over all char-types incl. host)
	outMess.pad				= 0;
	status = NSpMessage_Send(gNetGame, &outMess.h, kNSpSendFlag_Registered);	// send message
	if (status)
	{
		NetGameFatalError(kNetSequence_ErrorSendFailed);
	}
}



/********************* CLIENT TELL HOST LEVEL IS PREPARED *******************************/
//
// Called right beofre PlayArea().  This waits for the sync message from the other client players
// indicating that they are ready to start playing.
//

void ClientTellHostLevelIsPrepared(void)
{
OSStatus				status;
NetSyncMessage			outMess;
int						startTick = TickCount();

		/***********************************/
		/* TELL THE HOST THAT WE ARE READY */
		/***********************************/

	NSpClearMessageHeader(&outMess.h);
	outMess.h.to 			= kNSpHostID;										// send to this host
	outMess.h.what 			= kNetSyncMessage;									// set message type
	outMess.h.messageLen 	= sizeof(outMess);									// set size of message
	status = NSpMessage_Send(gNetGame, &outMess.h, kNSpSendFlag_Registered);	// send message
	if (status)
	{
		gNetSequenceState = kNetSequence_Error;
		return;
	}


		/**************************/
		/* WAIT FOR HOST TO REPLY */
		/**************************/
		//
		// We're in-game now, so don't switch back to NetGather!
		//

	gNetSequenceState = kNetSequence_ClientWaitForSyncFromHost;

	while (gNetSequenceState == kNetSequence_ClientWaitForSyncFromHost)
	{
		bool gotMess = UpdateNetSequence();

		Net_MaybeSendKeepAlive();										// CMR7 Stage 4: heartbeat so radios stay awake + hostLastHeard stays fresh while loading

		if ((TickCount() - startTick) > (60 * LOADING_TIMEOUT))			// if no response for a while, then time out
		{
			NetGameFatalError(kNetSequence_ErrorNoResponseFromHost);
			return;
		}

		if (!gotMess && gNetSequenceState != kNetSequence_GameLoop)
		{
			SDL_Delay(25);
		}
	}
}


#pragma mark -


/************** SEND HOST CONTROL INFO TO CLIENTS *********************/
//
// The host sends this at the beginning of each frame to all of the network clients.
// This data contains the gFramesPerSecond/Frac info plus the key controls state bitfields for each player.
//

void HostSend_ControlInfoToClients(void)
{
OSStatus						status;
short							i;

	GAME_ASSERT(gIsNetworkHost);

				/* BUILD MESSAGE */

	NSpClearMessageHeader(&gHostOutMess.h);

	gHostOutMess.h.to 			= kNSpAllPlayers;						// send to all clients
	gHostOutMess.h.what 		= kNetHostControlInfoMessage;			// set message type
	gHostOutMess.h.messageLen 	= sizeof(gHostOutMess);						// set size of message

	gHostOutMess.frameCounter	= gHostSendCounter++;					// send the frame counter & inc
	gHostOutMess.fps 			= gFramesPerSecond;						// fps
	gHostOutMess.fpsFrac		= gFramesPerSecondFrac;					// fps frac
	gHostOutMess.randomSeed		= MyRandomLong();						// send the host's current random value for sync verification

	gHostOutMess.simTick		= gSimulationFrame;

	for (i = 0; i < MAX_PLAYERS; i++)								// control bits
	{
		gHostOutMess.controlBits[i] = gPlayerInfo[i].controlBits;
		gHostOutMess.controlBitsNew[i] = gPlayerInfo[i].controlBits_New;	// host-derived edges (Host_ConsumeClientInputs / GetLocalKeyState / AI)
		gHostOutMess.analogSteering[i] = gPlayerInfo[i].analogSteering;

		if (gPlayerInfo[i].isComputer)
			gHostOutMess.pauseState[i] = 0; // Ensure bots/dropped players don't pause the game
		else
			gHostOutMess.pauseState[i] = gPlayerInfo[i].net.pauseState;

		if (!gPlayerInfo[i].objNode)
		{
			gHostOutMess.syncPos[i] = (OGLPoint3D) {0,0,0};
			gHostOutMess.syncRotY[i] = 0.0f;
		}
		else
		{
			gHostOutMess.syncPos[i] = gPlayerInfo[i].objNode->Coord;
			gHostOutMess.syncRotY[i] = gPlayerInfo[i].objNode->Rot.y;
		}

		// CMR7 telemetry / ack: last REAL input seq applied, live queue depth + target depth, input flags.
		gHostOutMess.ackInputSeq[i]	= sLastApplied[i].lastAppliedSeq;
		gHostOutMess.queueDepth[i]	= (uint8_t) Queue_Count(i);
		gHostOutMess.targetDepth[i]	= sDepth[i].targetDepth;
		gHostOutMess.inputFlags[i]	= sInputFlags[i];
	}

	Host_FillOutgoingEvents(&gHostOutMess, gHostOutMess.frameCounter);	// CMR7 Stage 4: drain pending frame-aligned events (become-bot)

			/* SEND IT */

	status = NSpMessage_Send(gNetGame, &gHostOutMess.h, kNSpSendFlag_Registered);
	if (status)
		NetGameFatalError(kNetSequence_ErrorSendFailed);
}


/************** GET NETWORK CONTROL INFO FROM HOST *********************/
//
// The client reads this from the host at the beginning of each frame.
// This data will contain the fps and control bitfield info for each player.
//

static Boolean Client_InGame_HandleHostControlInfoMessage(NetHostControlInfoMessageType* mess)
{
	GAME_ASSERT(gIsNetworkClient);

	if (!NetValidateHostControlPayload(mess, gNumRealPlayers))
	{
		RejectProtocolMessage(&mess->h);
		return false;
	}

	if (mess->frameCounter < gHostSendCounter)			// see if this is an old packet, possibly a duplicate.  If so, skip it
		return false;

	if (mess->frameCounter > gHostSendCounter)			// see if we skipped a packet; one must have gotten lost
	{
		NetGameFatalError(kNetSequence_ErrorLostPacket);
		return false;
	}

	gHostSendCounter++;									// inc host counter since the next packet we get will be +1

	gFramesPerSecond 		= mess->fps;
	gFramesPerSecondFrac 	= mess->fpsFrac;

	// In Variable Time Step mode, simTick will diverge. Do NOT assert.
	// But we can use it to log drift if needed.

	if (MyRandomLong() != mess->randomSeed)				// verify that host's random # is in sync with ours!
	{
		NetGameFatalError(kNetSequence_SeedDesync);
		return false;
	}

	for (int i = 0; i < MAX_PLAYERS; i++)					// control bits
	{
		gPlayerInfo[i].controlBits 		= mess->controlBits[i];
		gPlayerInfo[i].controlBits_New 	= mess->controlBitsNew[i];
		gPlayerInfo[i].analogSteering	= mess->analogSteering[i];
		gPlayerInfo[i].net.pauseState	= mess->pauseState[i];

		// SYNC POSITION & ROTATION (Rubber Banding)
		// Pull client state towards Auth Host state.
		if (gPlayerInfo[i].objNode)
		{
			ObjNode *car = gPlayerInfo[i].objNode;
			const float kSyncFactor = 0.2f;
			const float kPI = 3.14159265f;
			const float kPI2 = 6.28318530f;

			car->Coord.x += (mess->syncPos[i].x - car->Coord.x) * kSyncFactor;
			car->Coord.y += (mess->syncPos[i].y - car->Coord.y) * kSyncFactor;
			car->Coord.z += (mess->syncPos[i].z - car->Coord.z) * kSyncFactor;

			float rotDiff = fmodf(mess->syncRotY[i] - car->Rot.y, kPI2);
			if (rotDiff <= -kPI)
				rotDiff += kPI2;
			else if (rotDiff > kPI)
				rotDiff -= kPI2;
			
			car->Rot.y += rotDiff * kSyncFactor;
		}
	}

	// CMR7 Stage 4: record any host-broadcast frame-aligned events (deduped). The client applies them
	// from the shared table in StepGameSimulation at effectiveFrame, AFTER the seed check above and
	// BEFORE MoveEverything — exactly where the host applies its own copy.
	{
		uint8_t ec = mess->eventCount;
		for (int k = 0; k < ec; k++)
			RecordFrameEvent(mess->events[k].effectiveFrame, mess->events[k].type, mess->events[k].playerNum);
	}

	return true;
}


#pragma mark - Client host-packet ring (CMR7 Stage 3)

//
// CMR7 Stage 3: the client no longer blocks waiting for the host's per-frame control
// packet. Net_Pump drains every readable host packet (non-blocking) into this ordered
// 32-slot ring (~530ms @60fps); the PlayArea loop then consumes up to K_max per render
// frame, applying each via the VERBATIM Client_InGame_HandleHostControlInfoMessage and
// stepping the sim once per applied packet. An empty ring => hold-last-frame.
//
// The ring is NEVER drained past full: unread bytes stay in the kernel socket buffer
// (TCP backpressure) so a host control packet is never dropped — dropping a middle packet
// would make the next one trip the handler's lost-packet fatal (frameCounter > counter).
//

#define CLIENT_HOST_RING_SIZE	32
#define CLIENT_HOLD_BADGE_TICKS	15				// 250ms @60Hz of continuous host absence => show badge

// A ring slot carries EITHER a host control packet OR a verbatim copy of a non-control host
// message (e.g. kNSpPlayerLeft), so BOTH are applied in exact host-stream (TCP) order during
// catch-up. Applying a sim-affecting non-control message inline while control frames sit
// un-applied in the ring would draw the synced gSimRNG (ChooseTaggedPlayer) / mutate the
// simulated-car set at the wrong stream position => randomSeed desync FATAL.
typedef enum
{
	kRingEntry_Control = 0,				// host per-frame control packet (advances the sim)
	kRingEntry_Other,					// any other host message, dispatched via HandleOtherNetMessage
} HostRingEntryKind;

typedef struct
{
	HostRingEntryKind	kind;
	union
	{
		NetHostControlInfoMessageType	control;						// kRingEntry_Control
		uint8_t							other[kNSpMaxMessageLength];	// kRingEntry_Other: verbatim message bytes
	} u;																// union align (>=4) keeps `other` valid for NSpMessageHeader*
} HostRingEntry;

typedef struct
{
	int								head;			// next slot to consume
	int								tail;			// next slot to fill
	HostRingEntry					slots[CLIENT_HOST_RING_SIZE];
} HostPacketRing;									// empty: head==tail; full: (tail+1)%N==head

static HostPacketRing	sClientHostRing;			// static => zero-init; reset in ResetClientHostRing
static uint32_t			sLastHostArrivalTick = 0;	// tick of the most recent host packet PUSHED to the ring

static inline Boolean HostRing_Full(void)
{
	return ((sClientHostRing.tail + 1) % CLIENT_HOST_RING_SIZE) == sClientHostRing.head;
}

static Boolean HostRing_PushControl(const NetHostControlInfoMessageType* msg)
{
	if (HostRing_Full())
		return false;
	HostRingEntry* e = &sClientHostRing.slots[sClientHostRing.tail];
	e->kind = kRingEntry_Control;
	e->u.control = *msg;
	sClientHostRing.tail = (sClientHostRing.tail + 1) % CLIENT_HOST_RING_SIZE;
	return true;
}

static Boolean HostRing_PushOther(const NSpMessageHeader* msg)
{
	if (HostRing_Full())
		return false;
	uint32_t len = msg->messageLen;
	if (len < sizeof(NSpMessageHeader))		len = sizeof(NSpMessageHeader);		// never under-copy the header
	if (len > kNSpMaxMessageLength)			len = kNSpMaxMessageLength;			// clamp a garbage length
	HostRingEntry* e = &sClientHostRing.slots[sClientHostRing.tail];
	e->kind = kRingEntry_Other;
	memcpy(e->u.other, msg, len);
	sClientHostRing.tail = (sClientHostRing.tail + 1) % CLIENT_HOST_RING_SIZE;
	return true;
}

static Boolean HostRing_Pop(HostRingEntry* out)
{
	if (sClientHostRing.head == sClientHostRing.tail)
		return false;
	*out = sClientHostRing.slots[sClientHostRing.head];
	sClientHostRing.head = (sClientHostRing.head + 1) % CLIENT_HOST_RING_SIZE;
	return true;
}

// Clear the ring + hold timers so a second net game in the same process starts empty.
void ResetClientHostRing(void)
{
	sClientHostRing.head = 0;
	sClientHostRing.tail = 0;
	sLastHostArrivalTick = TickCount();
}

//
// Drain every readable host message into the ring (non-blocking). We only pull from the
// socket while the ring has space, so a host control packet is never dropped (TCP
// backpressure handles a sustained-full ring). Called from Net_Pump's client branch — the
// only in-game client socket drain.
//
// CRITICAL ORDERING: every NON-control host message is ALSO staged into the same ordered ring
// (not dispatched inline) so it is applied in exact host-stream order relative to the control
// frames during catch-up. CMR7 Stage 4 makes the client IGNORE an in-game relayed kNSpPlayerLeft
// (the host instead broadcasts a frame-aligned kEvBecomeBot event the client applies from the
// shared table at effectiveFrame — see ApplyPendingFrameEvents), so the leave no longer draws
// gSimRNG inline at all. Staging non-control messages in stream order is retained as the defensive
// invariant for any future sim-affecting message. Only kNSpGameTerminated, which merely tears the
// session down and never touches gSimRNG / the sim set, stays inline.
//
void Client_PumpHostPackets(void)
{
	if (!gIsNetworkClient || !gNetGame)
		return;

	while (!HostRing_Full())									// only Get() while we can store it
	{
		NSpMessageHeader* inMess = NSpMessage_Get(gNetGame);	// non-blocking recv

		if (inMess == NULL)
			break;												// socket drained

		if (inMess->what == kNetHostControlInfoMessage)
		{
			if (inMess->messageLen >= sizeof(NetHostControlInfoMessageType))		// don't copy a short message up to the full struct (mirror the host-side guard)
			{
				HostRing_PushControl((NetHostControlInfoMessageType*) inMess);	// guaranteed space (ring not full)
				sLastHostArrivalTick = TickCount();								// arrival time for the hold badge
			}
			// else: malformed/short control frame — drop it (released below)
		}
		else if (inMess->what == kNSpGameTerminated)
		{
			HandleOtherNetMessage(inMess);						// teardown only (no gSimRNG / sim-set touch): act now
			NSpMessage_Release(gNetGame, inMess);				// release before the loop reuses gNetGame (now nil after teardown)
			break;												// session torn down — stop draining
		}
		else
		{
			HostRing_PushOther(inMess);							// PlayerLeft / sync / etc.: apply in stream order
		}

		NSpMessage_Release(gNetGame, inMess);
	}
}

//
// Pop ONE staged host packet and apply it via the verbatim handler. Returns Applied
// (counter advanced -> caller steps the sim), Dup (old/duplicate, dropped by the handler
// -> no sim step), or Empty (ring drained). Does NOT step the simulation itself.
//
HostConsumeResult Client_ConsumeHostPacketFromRing(void)
{
	HostRingEntry entry;

	if (!HostRing_Pop(&entry))
		return kHostConsume_Empty;

	if (entry.kind == kRingEntry_Other)
	{
		// A non-control host message (e.g. kNSpPlayerLeft) staged in exact stream order. Dispatch it
		// at this FIFO position so any gSimRNG draw / sim-state change (ChooseTaggedPlayer, isComputer)
		// lands where the host applied it. It neither advances the host counter nor steps the sim, so
		// report Dup: the catch-up loop keeps draining the ring without burning a K_max slot or stepping.
		HandleOtherNetMessage((NSpMessageHeader*) entry.u.other);
		return kHostConsume_Dup;
	}

	Boolean applied = Client_InGame_HandleHostControlInfoMessage(&entry.u.control);	// VERBATIM apply (may also fatal inside)
	return applied ? kHostConsume_Applied : kHostConsume_Dup;
}

//
// True once no host packet has arrived for ~250ms of continuous absence. Keyed on ARRIVAL
// time (not stepped==0) so a frame that only popped duplicates still counts as having
// "heard" the host. Suppressed during the start-light countdown.
//
Boolean Client_IsHoldBadgeVisible(void)
{
	if (!gIsNetworkClient)
		return false;
	if (gNoCarControls)									// suppress during start-light countdown
		return false;
	return (TickCount() - sLastHostArrivalTick) > CLIENT_HOLD_BADGE_TICKS;
}

//
// True when the local machine should show a "connection degraded" HUD hint: a client whose host
// link has gone quiet (the ~250ms hold or the >1s badge), or a host with any peer quiet for >1s.
// Input substitution keeps the simulation alive in all these cases -- this is purely a visual cue
// so the player understands why a car is coasting/rubber-banding.
//
Boolean Net_IsConnectionBadgeVisible(void)
{
	if (!gNetGameInProgress)
		return false;

	if (gIsNetworkClient)
		return Client_IsHoldBadgeVisible() || gNetBadge[0];

	if (gIsNetworkHost)
	{
		for (int i = 0; i < MAX_PLAYERS; i++)
			if (gNetBadge[i])
				return true;
	}

	return false;
}


// (CMR7 Stage 4: ClientReceive_ControlInfoFromHost DELETED — the last blocking receive shim.
// The in-game loop is free-running (Net_Pump + ring catch-up), and Paused.c / the loading
// barriers now use the same non-blocking pump + wall-clock sampler. The 4-strike DATA_TIMEOUT
// fatal it carried is replaced by the per-connection lastHeard policy in NetCheck_ConnectionTimeouts.)


// (CMR7 Stage 3: Client_CheckIfMorePacketsWaiting DELETED — dead since the free-running
// receive rewrite; host packets are now drained into the ring by Client_PumpHostPackets.)


/************** CLIENT SEND CONTROL INFO TO HOST *********************/
//
// The client sends its current control state to the host. CMR7: a single packet per call,
// stamped with a client-owned monotonic inputSeq. The host derives edges and substitutes
// missing inputs, so the 8-frame redundancy history (dead weight on ordered TCP) is gone.
//

void ClientSend_ControlInfoToHost(void)
{
	GAME_ASSERT(gIsNetworkClient);

				/* BUILD MESSAGE */

	NSpClearMessageHeader(&gClientOutMess.h);

	gClientOutMess.h.to 			= kNSpHostID;							// send to Host
	gClientOutMess.h.what 			= kNetClientControlInfoMessage;			// set message type
	gClientOutMess.h.messageLen 	= sizeof(gClientOutMess);				// set size of message

	gClientOutMess.playerNum		= gMyNetworkPlayerNum;
	gClientOutMess.pauseState		= gPlayerInfo[gMyNetworkPlayerNum].net.pauseState;
	gClientOutMess.inputSeq			= gClientSendCounter[gMyNetworkPlayerNum]++;	// monotonic, client-owned
	gClientOutMess.lastHostFrameSeen= gHostSendCounter;							// RTT/diagnostics
	gClientOutMess.controlBits 		= gPlayerInfo[gMyNetworkPlayerNum].controlBits;
	gClientOutMess.analogSteering	= gPlayerInfo[gMyNetworkPlayerNum].analogSteering;

			/* SEND IT */

	NSpMessage_Send(gNetGame, &gClientOutMess.h, kNSpSendFlag_Registered);	// Stage 1 SendOrEnqueue: non-blocking
}

/*************** HOST PUMP CLIENT INPUTS ***********************/
//
// CMR7: non-blocking drain of every readable message. Client control packets are arrival-
// timestamped (feeding the per-client jitter estimator) and pushed into their per-player
// queue; everything else goes to the standard dispatcher. The host NEVER blocks here.
// Called from Net_Pump's host branch.
//

void Host_PumpClientInputs(void)
{
	if (!gIsNetworkHost || !gNetGame)
		return;

	double perfFreq = (double) SDL_GetPerformanceFrequency();

	for (unsigned packet = 0; packet < HOST_PACKET_BUDGET_PER_PUMP; packet++)
	{
		NSpMessageHeader* inMess = NSpMessage_Get(gNetGame);
		if (inMess == NULL)
			break;

		Boolean over = false;

		if (inMess->what == kNetClientControlInfoMessage)
		{
			NetClientControlInfoMessageType* cMsg = (NetClientControlInfoMessageType*) inMess;

				// Drop malformed / out-of-range / self / bot-slot packets before any array index.
				if (ValidateClientControlMessage(cMsg)
					&& cMsg->playerNum != gMyNetworkPlayerNum
					&& !gPlayerInfo[cMsg->playerNum].isComputer)
				{
					int p = cMsg->playerNum;

						/* FEED THE INTER-ARRIVAL JITTER RING (drives adaptive depth D_i) */

				uint64_t now = SDL_GetPerformanceCounter();
				if (sDepth[p].lastArrivalPC != 0 && perfFreq > 0.0)
				{
					float dt = (float)((double)(now - sDepth[p].lastArrivalPC) / perfFreq);
					sDepth[p].deltas[sDepth[p].head] = dt;
					sDepth[p].head = (sDepth[p].head + 1) % JITTER_WINDOW;
					if (sDepth[p].count < JITTER_WINDOW)
						sDepth[p].count++;
				}
				sDepth[p].lastArrivalPC = now;

					Queue_Push(p, cMsg);
				}
				else
				{
					RejectProtocolMessage(inMess);
				}
		}
		else
		{
			over = HandleOtherNetMessage(inMess);
		}

		NSpMessage_Release(gNetGame, inMess);

		if (over || !gNetGameInProgress || gNetGame == nil)		// game torn down mid-drain (e.g. everybody left)
			return;
	}
}


/*************** HOST INPUT CONSUME HELPERS ***********************/

static int CompareFloatAsc(const void* a, const void* b)
{
	float fa = *(const float*) a;
	float fb = *(const float*) b;
	return (fa < fb) ? -1 : ((fa > fb) ? 1 : 0);
}

// Commit a real applied packet as the new edge-derivation baseline + ack source.
static void Host_CommitApplied(int i, const NetClientControlInfoMessageType* m)
{
	HostClientInputState* S = &sLastApplied[i];
	S->controlBits		= m->controlBits;
	S->analogSteering	= m->analogSteering;
	S->pauseState		= m->pauseState;
	S->lastAppliedSeq	= m->inputSeq;
	S->valid			= true;
	S->subStreak		= 0;
	gClientSendCounter[i] = m->inputSeq + 1;			// bookkeeping mirror (stale-discard baseline)
}

// Recompute the P95-jitter-derived baseline depth (<=4Hz) and slowly decay the target depth.
static void Host_UpdateDepth(int i)
{
	double F = (gTargetFPS > 0) ? (1.0 / gTargetFPS) : (1.0 / 60.0);
	double now = (double) SDL_GetPerformanceCounter() / (double) SDL_GetPerformanceFrequency();

	if (now - sDepth[i].baselineSec >= 0.25)			// throttle the recompute to <=4Hz
	{
		int n = sDepth[i].count;
		if (n > 0)
		{
			float tmp[JITTER_WINDOW];
			for (int k = 0; k < n; k++)
			{
				float dev = sDepth[i].deltas[k] - (float) F;
				tmp[k] = (dev < 0.0f) ? -dev : dev;
			}
			qsort(tmp, n, sizeof(float), CompareFloatAsc);

			int idx = (int) ceilf(0.95f * (float) n) - 1;
			if (idx < 0) idx = 0;
			if (idx >= n) idx = n - 1;

			float p95 = tmp[idx];
			int dbase = (int) ceilf(p95 / (float) F) + 1;
			if (dbase < 1) dbase = 1;
			if (dbase > 8) dbase = 8;

			sDepth[i].baselineDepth = (uint8_t) dbase;
			if (dbase > (int) sDepth[i].targetDepth)
				sDepth[i].targetDepth = (uint8_t) dbase;
		}
		sDepth[i].baselineSec = now;
	}

	// Decay one frame per 2s while above the live baseline and above the floor.
	sDepth[i].decayAccumSec += (float) F;
	if (sDepth[i].decayAccumSec >= 2.0f
		&& sDepth[i].targetDepth > sDepth[i].baselineDepth
		&& sDepth[i].targetDepth > 1)
	{
		sDepth[i].targetDepth--;
		sDepth[i].decayAccumSec = 0.0f;
	}
}


/*************** HOST CONSUME CLIENT INPUTS ***********************/
//
// CMR7: top of the host frame, before HostSend. Resolves each remote client's input for THIS
// frame from its queue: SUBSTITUTE on underrun (hold last real input, never advance the ack),
// COALESCE a backlog down to D_i (OR-merging per-step edges), else APPLY exactly one packet.
// The host's own slot and bot/dropped slots are skipped (driven by GetLocalKeyState / AI).
//

void Host_ConsumeClientInputs(void)
{
	if (!gIsNetworkHost)
		return;

	memset(sInputFlags, 0, sizeof(sInputFlags));

	const uint32_t kMovementBits =
		(1u << kControlBit_Forward) | (1u << kControlBit_Backward) | (1u << kControlBit_Brakes);

			/* BOUNDED GRACE: re-poll a couple of times to catch near-miss kernel-buffered packets */

	for (int g = 0; g < GRACE_RETRIES; g++)
	{
		Boolean anyEmpty = false;
		for (int i = 0; i < MAX_PLAYERS; i++)
		{
			if (i == gMyNetworkPlayerNum || gPlayerInfo[i].isComputer)
				continue;
			if (Queue_Count(i) == 0)
			{
				anyEmpty = true;
				break;
			}
		}
		if (!anyEmpty)
			break;
		SDL_Delay(1);
		Net_Pump();									// re-drain (host branch pumps client inputs into the queues)
	}

	for (int i = 0; i < MAX_PLAYERS; i++)
	{
		if (i == gMyNetworkPlayerNum)				// host reads its own input at Step 3
			continue;
		if (gPlayerInfo[i].isComputer)				// bot/dropped -> AI drives the bits
			continue;

		HostClientInputState* S = &sLastApplied[i];
		int D = sDepth[i].targetDepth;
		if (D < 1) D = 1;

				/* DISCARD STALE PACKETS (dups / already-consumed pre-roll) */

		NetClientControlInfoMessageType* p;
		while ((p = Queue_Peek(i)) != NULL && S->valid && p->inputSeq <= S->lastAppliedSeq)	// S->valid guard: don't discard the very first packet (inputSeq 0) before any real input is applied
			Queue_Pop(i);

		int B = Queue_Count(i);

		if (B == 0)
		{
					/* ---- SUBSTITUTE (underrun): hold last real input, never advance the ack ---- */

			gPlayerInfo[i].controlBits		= S->valid ? S->controlBits : 0;
			gPlayerInfo[i].controlBits_New	= 0;								// no new edges while silent
			gPlayerInfo[i].analogSteering	= S->valid ? S->analogSteering : (OGLVector2D){0,0};
			gPlayerInfo[i].net.pauseState	= S->valid ? S->pauseState : 0;		// HELD: a radio blip never spuriously unpauses

			if (S->subStreak < 0xFFFF)
				S->subStreak++;

			if (S->subStreak > SUB_DECAY_AFTER)
			{
				// Graceful coast: decay the APPLIED bits/analog ONLY. Leave S->controlBits (the
				// edge baseline) intact so a still-held button does not re-fire as a NEW edge on
				// recovery (which would double-fire e.g. a weapon throw).
				gPlayerInfo[i].analogSteering.x *= 0.9f;
				gPlayerInfo[i].analogSteering.y *= 0.9f;
				gPlayerInfo[i].controlBits &= ~kMovementBits;
			}

			sInputFlags[i] |= INPUT_FLAG_SUBSTITUTED;
			if (sDepth[i].targetDepth < NET_MAX_INPUT_DEPTH)
				sDepth[i].targetDepth++;										// immediate underrun bump
			continue;
		}

		if (B > D + 1)
		{
					/* ---- COALESCE down to D: OR-merge per-step edges, apply the newest ---- */

			int toPop = B - D;
			uint32_t prev = S->controlBits;
			uint32_t merged = 0;
			NetClientControlInfoMessageType last;
			memset(&last, 0, sizeof(last));

			for (int k = 0; k < toPop; k++)
			{
				p = Queue_Peek(i);
				if (!p) break;
				merged |= Host_DeriveEdges(prev, p->controlBits);	// preserve EVERY press across the window
				prev = p->controlBits;
				last = *p;
				Queue_Pop(i);
			}

			gPlayerInfo[i].controlBits		= last.controlBits;
			gPlayerInfo[i].controlBits_New	= merged;
			gPlayerInfo[i].analogSteering	= last.analogSteering;
			gPlayerInfo[i].net.pauseState	= last.pauseState;

			Host_CommitApplied(i, &last);
			sInputFlags[i] |= INPUT_FLAG_COALESCED;
		}
		else
		{
					/* ---- APPLY exactly one real packet ---- */

			p = Queue_Peek(i);
			gPlayerInfo[i].controlBits		= p->controlBits;
			gPlayerInfo[i].controlBits_New	= Host_DeriveEdges(S->controlBits, p->controlBits);
			gPlayerInfo[i].analogSteering	= p->analogSteering;
			gPlayerInfo[i].net.pauseState	= p->pauseState;

			Host_CommitApplied(i, p);
			Queue_Pop(i);
		}

		Host_UpdateDepth(i);
	}
}


/*************** HOST INIT INPUT CONTROL ***********************/
//
// Called once by the host right before the game loop. ResetNetGameTransientState() has
// already zeroed the per-client state; here we seed each remote client's adaptive depth
// from its WiFi/wired hint (wired -> D=2, WiFi -> D=6). The host slot and bots are left at
// the default (0, treated as 1 by the consumer).
//

void Host_InitInputControl(void)
{
	for (int i = 0; i < MAX_PLAYERS; i++)
	{
		if (i == gMyNetworkPlayerNum || gPlayerInfo[i].isComputer)
			continue;
		sDepth[i].targetDepth = (sClientConnectionHint[i] == 1) ? 6 : 2;
	}
}


/*************** SAMPLE AND SEND LOCAL INPUT (CLIENT, WALL-CLOCK) ***********************/
//
// CMR7 (G1): the client's uplink is paced by the wall clock, decoupled from host-packet
// receipt, so a downlink stall never silences the uplink. Emits one input packet per F
// seconds, bursting up to MAX_SAMPLE_BURST to refill the host's backlog after a stall, and
// resyncing after a long wall-clock gap (app suspend) to avoid a flood of stale packets.
//

void SampleAndSendLocalInput(Boolean* outSchedulePause)
{
	GAME_ASSERT(gIsNetworkClient);

	double F = (gTargetFPS > 0) ? (1.0 / gTargetFPS) : (1.0 / 60.0);
	double now = (double) SDL_GetPerformanceCounter() / (double) SDL_GetPerformanceFrequency();

	if (sNextSampleTime == 0.0 || (now - sNextSampleTime) > 0.250)	// first call / suspend resync
		sNextSampleTime = now;

	int burst = 0;
	while (now >= sNextSampleTime && burst < MAX_SAMPLE_BURST)
	{
		ReadKeyboard();
		GetLocalKeyState();
		// GetNewNeedStateAnyP is edge-triggered: OR into the caller's flag so a burst can't drop the edge.
		if (GetNewNeedStateAnyP(kNeed_UIPause))
			*outSchedulePause = true;
		gPlayerInfo[gMyNetworkPlayerNum].net.pauseState = *outSchedulePause;

		ClientSend_ControlInfoToHost();				// inputSeq = gClientSendCounter[me]++

		sNextSampleTime += F;
		burst++;
		now = (double) SDL_GetPerformanceCounter() / (double) SDL_GetPerformanceFrequency();
	}
}


#pragma mark -


/******************** GET CONNECTION HINT ************************/
//
// Returns 1 if likely WiFi, 0 if wired.
//
//
// DETECT WIFI CONNECTION
// Returns: 1 if WiFi, 0 if Wired/Unknown
//
int Net_GetConnectionHint(void)
{
#if defined(__APPLE__) && !defined(__IOS__) && !defined(__TVOS__)
	// macOS Implementation using SystemConfiguration
	int isWifi = 0;
	CFArrayRef interfaces = SCNetworkInterfaceCopyAll();
	if (interfaces)
	{
		CFIndex count = CFArrayGetCount(interfaces);
		for (CFIndex i = 0; i < count; i++)
		{
			SCNetworkInterfaceRef interface = (SCNetworkInterfaceRef)CFArrayGetValueAtIndex(interfaces, i);
			CFStringRef interfaceType = SCNetworkInterfaceGetInterfaceType(interface);
			
			if (CFStringCompare(interfaceType, kSCNetworkInterfaceTypeIEEE80211, 0) == kCFCompareEqualTo)
			{
				isWifi = 1;
				break;
			}
		}
		CFRelease(interfaces);
	}
	return isWifi;

#elif defined(__IOS__) || defined(__TVOS__) || defined(__ANDROID__)
    return 1; // Assume WiFi/Wireless for now

#elif defined(_WIN32)
	// Windows Implementation using wlanapi
	HANDLE hClient = NULL;
	DWORD dwMaxClient = 2; // WLAN_API_VERSION_2_0
	DWORD dwCurVersion = 0;
	DWORD dwResult = 0;
	int isWifi = 0;

	dwResult = WlanOpenHandle(dwMaxClient, NULL, &dwCurVersion, &hClient);
	if (dwResult == ERROR_SUCCESS)
	{
		PWLAN_INTERFACE_INFO_LIST pIfList = NULL;
		dwResult = WlanEnumInterfaces(hClient, NULL, &pIfList);
		if (dwResult == ERROR_SUCCESS)
		{
			if (pIfList->dwNumberOfItems > 0)
			{
				// Found a wireless interface. Check if any are connected?
				// For now, presence of interface is hint enough to enable redundancy safety.
				for (int i = 0; i < (int)pIfList->dwNumberOfItems; i++)
				{
					if (pIfList->InterfaceInfo[i].isState == wlan_interface_state_connected)
					{
						isWifi = 1;
						break;
					}
				}
			}
			WlanFreeMemory(pIfList);
		}
		WlanCloseHandle(hClient, NULL);
	}
	return isWifi;

#else
	// Linux / Default Implementation
	FILE *fp = fopen("/proc/net/wireless", "r");
	if (fp) {
		char line[256];
		int lineCount = 0;
		int isWifi = 0;
		while (fgets(line, sizeof(line), fp)) {
			lineCount++;
			if (lineCount > 2) { 
				isWifi = 1; // Content implies interface exists.
				break;
			}
		}
		fclose(fp);
		return isWifi;
	}
	return 0;
#endif
}


/********************* PLAYER BROADCAST VEHICLE TYPE *******************************/
//
// Tell all of the other net players what character type we want to be.
//

void PlayerBroadcastVehicleType(void)
{
OSStatus					status;
NetPlayerCharTypeMessage	outMess;


				/* BUILD MESSAGE */

	NSpClearMessageHeader(&outMess.h);

	outMess.h.to 			= kNSpAllPlayers;						// send to all clients
	outMess.h.what 			= kNetPlayerCharTypeMessage;			// set message type
	outMess.h.messageLen 	= sizeof(outMess);						// set size of message

	outMess.playerNum		= gMyNetworkPlayerNum;					// player #
	outMess.vehicleType		= gPlayerInfo[gMyNetworkPlayerNum].vehicleType;
	outMess.sex				= gPlayerInfo[gMyNetworkPlayerNum].sex;
	outMess.skin			= gPlayerInfo[gMyNetworkPlayerNum].skin;
	outMess.refreshRate		= OGL_GetMonitorRefreshRate();
	outMess.connectionType	= Net_GetConnectionHint();

			/* SEND IT */

	status = NSpMessage_Send(gNetGame, &outMess.h, kNSpSendFlag_Registered);
	if (status)
	{
		NetGameFatalError(kNetSequence_ErrorSendFailed);
		return;
	}
}

/***************** GET VEHICLE SELECTION FROM NET PLAYERS ***********************/

// Returns true if the player aborted or the net game was torn down while waiting for the
// other players' vehicle selections, so the caller can bail cleanly instead of starting a
// broken match.
Boolean GetVehicleSelectionFromNetPlayers(void)
{
	ClearPlayerSyncMask();
	MarkPlayerSynced(NSpPlayer_GetMyID(gNetGame));		// we have our own local info already

	gNetSequenceState = kNetSequence_WaitingForPlayerVehicles;
	return DoNetGatherScreen();							// true == aborted/torn down
}




/******************* HANDLE OTHER NET MESSAGE ***********************/
//
// Called when other message handler's get a message that they don't expect to get.
//
// OUTPUT: returns TRUE if game terminated
//

Boolean HandleOtherNetMessage(NSpMessageHeader	*message)
{
	printf("%s: %s\n", __func__, NSp4CCString(message->what));

	switch(message->what)
	{

					/* AN ERROR MESSAGE */

		case	kNSpError:
				printf("HandleOtherNetMessage: kNSpError (ignored)\n");
				break;


					/* A PLAYER UNEXPECTEDLY HAS LEFT THE GAME */

		case	kNSpPlayerLeft:
				ForgetPlayerSync(((NSpPlayerLeftMessage*) message)->playerID);
				if (gNetSequenceState == kNetSequence_GameLoop)
				{
					// CMR7 Stage 4 (G3): a running sim must convert frame-aligned, not at TCP-arrival time.
					// The HOST schedules + re-broadcasts a kEvBecomeBot event; EVERY peer (host included)
					// applies it from the shared table at the identical sim frame. A relayed leave on a
					// CLIENT is ignored here — the host's broadcast event drives the conversion deterministically.
					if (gIsNetworkHost)
						ScheduleBecomeBotFromLeave((NSpPlayerLeftMessage *)message);
				}
				else
				{
					// Lobby / loading barriers: no running sim to desync, so convert immediately.
					PlayerUnexpectedlyLeavesGame((NSpPlayerLeftMessage *)message);
				}
				break;

					/* THE HOST HAS UNEXPECTEDLY LEFT THE GAME */

		case	kNSpGameTerminated:
				printf("Game Terminated: The Host has unexpectedly quit the game.\n");
				EndNetworkGame();
				switch (((NSpGameTerminatedMessage*) message)->reason)
				{
					case kNSpGameTerminated_YouGotKicked:
						gNetSequenceState = kNetSequence_ClientOfflineBecauseKicked;
						break;

					case kNSpGameTerminated_NetworkError:
						gNetSequenceState = kNetSequence_ClientOfflineBecauseHostUnreachable;
						break;

					case kNSpGameTerminated_HostBailed:
					default:
						gNetSequenceState = kNetSequence_ClientOfflineBecauseHostBailed;
						break;
				}
				gGameOver = true;
				break;

					/* NULL PACKET */

		case	kNetSyncMessage:
				break;

					/* CMR7 Stage 4: HEARTBEAT — lastHeard was already refreshed in the NetLow Get path */

		case	kNetKeepAliveMessage:
				break;

				/* UNEXPECTED INTERNAL MESSAGE TYPES                                          */
				/* A peer, or a relayed kNSpAllPlayers message, can deliver any of these in   */
				/* game. Log and ignore rather than handing a remote peer a DoFatalAlert that  */
				/* would crash the host and every client.                                      */

		case	kNSpJoinRequest:
		case	kNSpJoinApproved:
		case	kNSpJoinDenied:
		case	kNSpPlayerJoined:
		case	kNSpHostChanged:
		case	kNSpGroupCreated:
		case	kNSpGroupDeleted:
		case	kNSpPlayerAddedToGroup:
		case	kNSpPlayerRemovedFromGroup:
		case	kNSpPlayerTypeChanged:
		default:
				printf("HandleOtherNetMessage: unexpected message '%s' (ignored)\n", NSp4CCString(message->what));
				break;
	}

	return(gGameOver);
}


/***************** PLAYER UNEXPECTEDLY LEAVES GAME ***********************/
//
// Called when HandleOtherNetMessage() gets a kNSpPlayerLeft message from one of the other players.
//
// INPUT: playerID = ID# of player that sent message
//

static void PlayerUnexpectedlyLeavesGame(NSpPlayerLeftMessage *mess)
{
	// CMR7 Stage 4: the IMMEDIATE (lobby / loading-barrier) leave path — no running sim to desync.
	// In-game leaves go through the frame-aligned ScheduleBecomeBotFromLeave path instead.
	int i = FindHumanByNSpPlayerID(mess->playerID);

	if (i < 0)
	{
		// A leave for an unknown / already-removed player id: e.g. a client that dropped during
		// the lobby before ids were assigned, or a duplicate leave. Ignore it rather than taking
		// the whole session down with a fatal alert.
		printf("PlayerUnexpectedlyLeavesGame: no matching player id #%d; ignoring.\n", (int) mess->playerID);
		return;
	}

	ApplyBecomeBot(i);
}





Boolean IsNetGamePaused(void)
{
	if (!gNetGameInProgress)
	{
		return false;
	}

	for (int i = 0; i < MAX_PLAYERS; i++)
	{
		if (!gPlayerInfo[i].isComputer && gPlayerInfo[i].net.pauseState)
		{
			return true;
		}
	}

	return false;
}
