/****************************/
/* LOW-LEVEL NETWORKING     */
/* (c)2022 Iliyas Jorio     */
/****************************/

#if _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <mstcpip.h>  // For tcp_keepalive struct and SIO_KEEPALIVE_VALS
typedef int ssize_t;
typedef int socklen_t;
#define MSG_NOSIGNAL 0
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <netinet/tcp.h>
#endif

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include <SDL3/SDL.h>

#include "game.h"
#include "network.h"

int gNetPort = 49959;
#define LOBBY_BROADCAST_INTERVAL 1.0f

#define PENDING_CONNECTIONS_QUEUE 10

#define MAX_LOBBIES 5

#define NSPGAME_COOKIE32 'NSpG'

#if _WIN32
#define kSocketError_WouldBlock			WSAEWOULDBLOCK
#define kSocketError_AddressInUse		WSAEADDRINUSE
#else
#define kSocketError_WouldBlock			EAGAIN
#define kSocketError_AddressInUse		EADDRINUSE
#endif

// Socket buffer sizes for burst absorption
#define SOCKET_SNDBUF_SIZE 65536
#define SOCKET_RCVBUF_SIZE 65536

typedef enum
{
	kNSpPlayerState_Offline,
	kNSpPlayerState_Me,
	kNSpPlayerState_ConnectedPeer,
	kNSpPlayerState_AwaitingHandshake,
} NSpPlayerState;

typedef struct NSpPlayer
{
	NSpPlayerID					id;
	NSpPlayerState				state;
	sockfd_t					sockfd;
	char						name[kNSpPlayerNameLength];
} NSpPlayer;

typedef struct NSpGame
{
	sockfd_t					hostAdvertiseSocket;
	sockfd_t					hostListenSocket;
	sockfd_t					clientToHostSocket;

	bool						isHosting;
	bool						isAdvertising;
	NSpPlayerID					myID;

	NSpPlayer					players[MAX_CLIENTS];

	float						timeToReadvertise;

	uint32_t					cookie;
	
	int							nextPollIndex;
} NSpGame;

typedef struct
{
	struct sockaddr_in			hostAddr;
} LobbyInfo;

typedef struct NSpSearch
{
	sockfd_t					listenSocket;
	int							numGamesFound;
	LobbyInfo					gamesFound[MAX_LOBBIES];
} NSpSearch;

static uint32_t gOutboundMessageCounter = 1;

static int gLastQueriedSocketError = 0;

static NSpGameReference JoinLobby(const LobbyInfo* lobby);
static sockfd_t CreateTCPSocket(bool bindIt);
static NSpGame* NSpGame_Unbox(NSpGameReference gameRef);
static NSpGameReference NSpGame_Alloc(void);
static void NSpPlayer_Clear(NSpPlayer* player);
static int SendOnSocket(sockfd_t sockfd, NSpMessageHeader* header);

static NSpPlayerID NSpGame_ClientSlotToID(NSpGameReference gameRef, int slot);
static int NSpGame_ClientIDToSlot(NSpGameReference gameRef, NSpPlayerID id);
static NSpPlayer* NSpGame_GetPlayerFromID(NSpGameReference gameRef, NSpPlayerID id);

#pragma mark - Cross-platform compat

int GetSocketError(void)
{
#if _WIN32
	gLastQueriedSocketError = WSAGetLastError();
#else
	gLastQueriedSocketError = errno;
#endif
	return gLastQueriedSocketError;
}

int GetLastSocketError(void)
{
	return gLastQueriedSocketError;
}

bool IsSocketValid(sockfd_t sockfd)
{
	return sockfd != INVALID_SOCKET;
}

bool MakeSocketNonBlocking(sockfd_t sockfd)
{
#if _WIN32
	u_long flags = 1;  // 0=blocking, 1=non-blocking
	return NO_ERROR == ioctlsocket(sockfd, FIONBIO, &flags);
#else
	int flags = fcntl(sockfd, F_GETFL, 0);
	flags |= O_NONBLOCK;
	return -1 != fcntl(sockfd, F_SETFL, flags);
#endif
}

bool CloseSocket(sockfd_t* sockfdPtr)
{
	if (!sockfdPtr)
	{
		return false;
	}

	if (!IsSocketValid(*sockfdPtr))
	{
		return false;
	}

#if _WIN32
	closesocket(*sockfdPtr);
#else
	close(*sockfdPtr);
#endif

	printf("Closed socket %d.\n", *sockfdPtr);

	*sockfdPtr = INVALID_SOCKET;

	return true;
}

// Apply performance/robustness socket options to a TCP socket
static void ApplyTCPSocketOptions(sockfd_t sockfd)
{
	int flag = 1;

	// Disable Nagle's algorithm for low latency
	if (-1 == setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, (void *)&flag, sizeof(int)))
	{
		printf("Warning: failed to set TCP_NODELAY: %d\n", GetSocketError());
	}

	// Enable keepalive for connection health monitoring
	if (-1 == setsockopt(sockfd, SOL_SOCKET, SO_KEEPALIVE, (void *)&flag, sizeof(int)))
	{
		printf("Warning: failed to set SO_KEEPALIVE: %d\n", GetSocketError());
	}

#if !_WIN32
	// Aggressive keepalive settings (Linux/macOS)
#ifdef TCP_KEEPIDLE
	int keepidle = 5;  // 5 seconds idle before probing
	setsockopt(sockfd, IPPROTO_TCP, TCP_KEEPIDLE, &keepidle, sizeof(int));
#endif
#ifdef TCP_KEEPINTVL
	int keepintvl = 1;  // 1 second between probes
	setsockopt(sockfd, IPPROTO_TCP, TCP_KEEPINTVL, &keepintvl, sizeof(int));
#endif
#ifdef TCP_KEEPCNT
	int keepcnt = 3;  // 3 probes before giving up
	setsockopt(sockfd, IPPROTO_TCP, TCP_KEEPCNT, &keepcnt, sizeof(int));
#endif

	// Disable delayed ACKs for lower latency (Linux)
#ifdef TCP_QUICKACK
	setsockopt(sockfd, IPPROTO_TCP, TCP_QUICKACK, &flag, sizeof(int));
#endif

	// Limit unsent data to reduce buffer bloat on high-bandwidth links (Linux 3.12+)
#ifdef TCP_NOTSENT_LOWAT
	int lowat = 16384;
	setsockopt(sockfd, IPPROTO_TCP, TCP_NOTSENT_LOWAT, &lowat, sizeof(int));
#endif
#else // _WIN32
	// Windows-specific aggressive keepalive settings
	// Uses SIO_KEEPALIVE_VALS ioctl - struct tcp_keepalive is from <mstcpip.h>
	struct tcp_keepalive keepalive_vals = {
		.onoff = 1,
		.keepalivetime = 5000,   // 5 seconds idle
		.keepaliveinterval = 1000 // 1 second interval
	};
	DWORD bytes_returned = 0;
	WSAIoctl(sockfd, SIO_KEEPALIVE_VALS, &keepalive_vals, sizeof(keepalive_vals),
		NULL, 0, &bytes_returned, NULL, NULL);
#endif // _WIN32

	// Increase send/receive buffer sizes for burst absorption
	int sndbuf = SOCKET_SNDBUF_SIZE;
	int rcvbuf = SOCKET_RCVBUF_SIZE;
	setsockopt(sockfd, SOL_SOCKET, SO_SNDBUF, (void *)&sndbuf, sizeof(int));
	setsockopt(sockfd, SOL_SOCKET, SO_RCVBUF, (void *)&rcvbuf, sizeof(int));
}

static const char* FormatAddress(struct sockaddr_in hostAddr)
{
	static char hostname[128];

	snprintf(hostname, sizeof(hostname), "[EMPTY]");
	inet_ntop(hostAddr.sin_family, &hostAddr.sin_addr, hostname, sizeof(hostname));

	snprintfcat(hostname, sizeof(hostname), ":%d", hostAddr.sin_port);

	return hostname;
}

const char* NSp4CCString(uint32_t fourcc)
{
	static char cstr[5];
	cstr[0] = (fourcc >> 24) & 0xFF;
	cstr[1] = (fourcc >> 16) & 0xFF;
	cstr[2] = (fourcc >> 8) & 0xFF;
	cstr[3] = (fourcc) & 0xFF;
	return cstr;
}

static void CopyPlayerName(char* dest, const char* src)
{
	memset(dest, 0, kNSpPlayerNameLength);
	strncpy(dest, src, kNSpPlayerNameLength);
	dest[kNSpPlayerNameLength-1] = '\0';
}

#pragma mark - Message creation helper

static Ptr _AllocMessageHelper(int size, int what, NSpPlayerID from, NSpPlayerID to)
{
	Ptr x = AllocPtrClear(size);
	NSpMessageHeader* header = (NSpMessageHeader*) x;
	NSpClearMessageHeader(header);
	header->messageLen = size;
	header->what = what;
	header->from = from;
	header->to = to;
	return x;
}

#define AllocMessage(T, from, to) ((T##Message*) _AllocMessageHelper(sizeof(T##Message), k##T, (from), (to)))

#pragma mark - Lobby broadcast

static sockfd_t CreateUDPBroadcastSocket(void)
{
	sockfd_t sockfd = INVALID_SOCKET;

	// Use protocol 0 (default) for robustness
	sockfd = socket(AF_INET, SOCK_DGRAM, 0);
	if (!IsSocketValid(sockfd))
	{
		GetSocketError();
#if _WIN32
		SDL_Log("%s: socket(UDP) failed: %d", __func__, gLastQueriedSocketError);
#else
		const char *errStr = strerror(gLastQueriedSocketError);
		SDL_Log("%s: socket(UDP) failed: %d (%s)", 
			__func__, gLastQueriedSocketError, errStr ? errStr : "unknown");
#endif
		return INVALID_SOCKET;
	}

	// 1. Set SO_BROADCAST
	int broadcast = 1;
	int sockoptRC = setsockopt(sockfd, SOL_SOCKET, SO_BROADCAST, (void*) &broadcast, sizeof(broadcast));
	if (-1 == sockoptRC)
	{
		GetSocketError();
		SDL_Log("%s: setsockopt(SO_BROADCAST) failed: %d", __func__, gLastQueriedSocketError);
		goto fail;
	}

	// 2. Set SO_REUSEADDR (Critical for Android/Linux broadcast listeners)
	int reuse = 1;
	sockoptRC = setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, (void*) &reuse, sizeof(reuse));
	if (-1 == sockoptRC)
	{
		GetSocketError();
		SDL_Log("%s: setsockopt(SO_REUSEADDR) failed: %d", __func__, gLastQueriedSocketError);
		goto fail;
	}

	if (!MakeSocketNonBlocking(sockfd))
	{
		GetSocketError();
		SDL_Log("%s: MakeSocketNonBlocking failed: %d", __func__, gLastQueriedSocketError);
		goto fail;
	}

	SDL_Log("Created UDP socket %d.", (int) sockfd);
	return sockfd;

fail:
	CloseSocket(&sockfd);
	return sockfd;
}



#pragma mark - Join lobby

static NSpGameReference JoinLobby(const LobbyInfo* lobby)
{
	printf("%s: %s\n", __func__, FormatAddress(lobby->hostAddr));

	int sockfd = INVALID_SOCKET;
	
	sockfd = CreateTCPSocket(false);
	if (!IsSocketValid(sockfd))
	{
		printf("%s: socket failed: %d\n", __func__, GetSocketError());
		goto fail;
	}

	struct sockaddr_in bindAddr =
	{
		.sin_family = AF_INET,
		.sin_port = htons(gNetPort),//lobby->hostAddr.sin_port,
		.sin_addr.s_addr = lobby->hostAddr.sin_addr.s_addr,
	};

	int connectRC = connect(sockfd, (const struct sockaddr*) &bindAddr, sizeof(bindAddr));

	if (connectRC == -1)
	{
		printf("%s: connect failed: %d\n", __func__, GetSocketError());
		goto fail;
	}

	// make it blocking AFTER connecting
	if (!MakeSocketNonBlocking(sockfd))
	{
		printf("%s: non-blocking failed: %d\n", __func__, GetSocketError());
		goto fail;
	}

	NSpJoinRequestMessage* joinRequestMessage = AllocMessage(NSpJoinRequest, kNSpUnspecifiedEndpoint, kNSpHostID);
	snprintf(joinRequestMessage->name, sizeof(joinRequestMessage->name), "CLIENT");

	int rc = SendOnSocket(sockfd, &joinRequestMessage->header);
	NSpMessage_Release(NULL, &joinRequestMessage->header);

	if (rc != kNSpRC_OK)
	{
		goto fail;
	}

	NSpGameReference gameRef = NSpGame_Alloc();
	NSpGame* game = NSpGame_Unbox(gameRef);
	game->isHosting = false;
	game->clientToHostSocket = sockfd;
	return gameRef;

fail:
	CloseSocket(&sockfd);
	return NULL;
}

#pragma mark - Host lobby

NSpPlayerID NSpGame_AcceptNewClient(NSpGameReference gameRef)
{
	int newClientSlot = -1;
	NSpPlayerID newPlayerID = kNSpUnspecifiedEndpoint;
	NSpGame* game = NSpGame_Unbox(gameRef);

	GAME_ASSERT(game->isHosting);

	sockfd_t newSocket = INVALID_SOCKET;

	newSocket = accept(game->hostListenSocket, NULL, NULL);

	if (!IsSocketValid(newSocket))	// nobody's trying to connect right now
	{
		goto fail;
	}

	// make the socket non-blocking
	if (!MakeSocketNonBlocking(newSocket))
	{
		goto fail;
	}

	// Apply all performance/robustness socket options
	ApplyTCPSocketOptions(newSocket);

	// Find vacant player slot
	for (int i = 0; i < MAX_PLAYERS; i++)
	{
		if (game->players[i].state == kNSpPlayerState_Offline)
		{
			newClientSlot = i;
			newPlayerID = NSpGame_ClientSlotToID(gameRef, newClientSlot);
			break;
		}
	}

	if (newClientSlot >= 0)
	{
		// Create new player
		NSpPlayer* newPlayer = &game->players[newClientSlot];

		newPlayer->id			= newPlayerID;
		newPlayer->state		= kNSpPlayerState_AwaitingHandshake;
		newPlayer->sockfd		= newSocket;
		snprintf(newPlayer->name, sizeof(newPlayer->name), "PLAYER %d", newPlayer->id);
	}
	else
	{
		// All slots used up
		printf("%s: A new client wants to connect, but the game is full!\n", __func__);

		NSpJoinDeniedMessage* deniedMessage = AllocMessage(NSpJoinDenied, kNSpHostID, kNSpUnspecifiedEndpoint);
		snprintf(deniedMessage->reason, sizeof(deniedMessage->reason), "THE GAME IS FULL.");

		SendOnSocket(newSocket, &deniedMessage->header);
		NSpMessage_Release(gameRef, &deniedMessage->header);

		goto fail;
	}

	printf("%s: Accepted client #%d on new socket %d.\n", __func__, newPlayerID, (int) newSocket);

	return newPlayerID;

fail:
	CloseSocket(&newSocket);
	return -1;
}

int NSpGame_StopAcceptingNewClients(NSpGameReference gameRef)
{
	NSpGame* game = NSpGame_Unbox(gameRef);

	if (!game)
	{
		return kNSpRC_NoGame;
	}

	GAME_ASSERT(game->isHosting);

	if (!IsSocketValid(game->hostListenSocket))
	{
		return kNSpRC_BadState;
	}

	CloseSocket(&game->hostListenSocket);
	return kNSpRC_OK;
}

#pragma mark - Message socket

static sockfd_t CreateTCPSocket(bool bindIt)
{
	// Try protocol 0 (default) instead of IPPROTO_TCP, just in case
	sockfd_t sockfd = socket(AF_INET, SOCK_STREAM, 0);
	
	if (!IsSocketValid(sockfd))
	{
		GetSocketError(); // Capture errno
#if _WIN32
		SDL_Log("%s: socket(AF_INET=%d, SOCK_STREAM=%d) failed: %d", 
			__func__, AF_INET, SOCK_STREAM, gLastQueriedSocketError);
#else
		const char *errStr = strerror(gLastQueriedSocketError);
		SDL_Log("%s: socket(AF_INET=%d, SOCK_STREAM=%d) failed: %d (%s)", 
			__func__, AF_INET, SOCK_STREAM, gLastQueriedSocketError, errStr ? errStr : "unknown");
#endif
		return INVALID_SOCKET;
	}

	if (bindIt && !MakeSocketNonBlocking(sockfd))
	{
		GetSocketError();
		SDL_Log("%s: MakeSocketNonBlocking failed: %d", __func__, gLastQueriedSocketError);
		CloseSocket(&sockfd);
		return INVALID_SOCKET;
	}

	// Apply all performance/robustness socket options
	ApplyTCPSocketOptions(sockfd);

	if (bindIt)
	{
		struct sockaddr_in bindAddr;
		memset(&bindAddr, 0, sizeof(bindAddr));
		bindAddr.sin_family = AF_INET;
		bindAddr.sin_port = htons(gNetPort);
		bindAddr.sin_addr.s_addr = INADDR_ANY;

		if (bind(sockfd, (struct sockaddr*)&bindAddr, sizeof(bindAddr)) != 0)
		{
			GetSocketError();
			SDL_Log("%s: bind failed: %d", __func__, gLastQueriedSocketError);
			CloseSocket(&sockfd);
			return INVALID_SOCKET;
		}
	}

	SDL_Log("Created TCP socket %d.", (int) sockfd);

	return sockfd;
}

#pragma mark - Robust recv helper

// Receive exactly 'len' bytes into 'buf', handling partial reads (common on WiFi).
// Returns:
//   > 0: success (number of bytes received, will equal 'len')
//   = 0: peer disconnected
//   < 0: error (check GetSocketError())
static ssize_t RecvAll(sockfd_t sockfd, void* buf, size_t len)
{
	size_t total = 0;
	char* ptr = (char*)buf;
	const Uint64 startTime = SDL_GetTicks();
	const Uint64 timeoutMs = 5000;  // 5 second timeout to prevent infinite loops

	while (total < len)
	{
		// Check for timeout
		if (SDL_GetTicks() - startTime > timeoutMs)
		{
			printf("%s: timeout after %llu ms waiting for %zu bytes\n", 
				__func__, (unsigned long long)timeoutMs, len - total);
			return -1;
		}

		ssize_t n = recv(sockfd, ptr + total, len - total, MSG_NOSIGNAL);

		if (n == 0)
		{
			// Peer disconnected
			return 0;
		}
		else if (n < 0)
		{
			int err = GetSocketError();
			if (err == kSocketError_WouldBlock)
			{
				// Non-blocking socket would block; wait a tiny bit and retry
				SDL_Delay(1);
				continue;
			}
			// Real error
			return -1;
		}

		total += (size_t)n;
	}

#if !_WIN32
	// Re-enable TCP_QUICKACK after each recv (it's a per-operation hint on Linux)
#ifdef TCP_QUICKACK
	int flag = 1;
	setsockopt(sockfd, IPPROTO_TCP, TCP_QUICKACK, &flag, sizeof(int));
#endif
#endif

	return (ssize_t)total;
}

#pragma mark - Basic message

void NSpClearMessageHeader(NSpMessageHeader* h)
{
	memset(h, 0, sizeof(NSpMessageHeader));
	h->version		= kNSpCMRProtocol4CC;
	h->when			= 0;
	h->id			= gOutboundMessageCounter;
	h->from			= kNSpUnspecifiedEndpoint;
	h->to			= kNSpUnspecifiedEndpoint;
	h->messageLen	= 0xBADBABEE;
	h->what			= kNSpUndefinedMessage;

	gOutboundMessageCounter++;
}

static NSpMessageHeader* PollSocket(sockfd_t sockfd, bool* outBrokenPipe)
{
	NSpMessageHeader* outMessage = NULL;
	bool brokenPipe = false;

	char messageBuf[kNSpMaxMessageLength];

	// Peek at the header first to check if any data is available (non-blocking)
	ssize_t recvRC = recv(
		sockfd,
		messageBuf,
		sizeof(NSpMessageHeader),
		MSG_NOSIGNAL | MSG_PEEK
	);

	// If received 0 bytes, our peer is probably gone
	if (recvRC == 0)
	{
		brokenPipe = true;
		goto bye;
	}

	// if -1, probably EAGAIN since our sockets are non-blocking - no data available
	if (recvRC == -1)
	{
		goto bye;
	}

	// We have at least some data - now use RecvAll to get the full header reliably
	recvRC = RecvAll(sockfd, messageBuf, sizeof(NSpMessageHeader));
	if (recvRC == 0)
	{
		brokenPipe = true;
		goto bye;
	}
	if (recvRC < 0)
	{
		printf("%s: error reading header: %d\n", __func__, GetLastSocketError());
		goto bye;
	}

	NSpMessageHeader* header = (NSpMessageHeader*) messageBuf;

	if (header->version != kNSpCMRProtocol4CC)
	{
		printf("%s: bad protocol %08x\n", __func__, header->version);
		goto bye;
	}

	if (header->messageLen > kNSpMaxPayloadLength
		|| header->messageLen < sizeof(NSpMessageHeader))
	{
		printf("%s: invalid message length %u\n", __func__, header->messageLen);
		goto bye;
	}

	ssize_t payloadLen = header->messageLen - sizeof(NSpMessageHeader);

	// Read payload if there's more to the message than just the header
	if (payloadLen > 0)
	{
		// Use RecvAll to ensure we get the complete payload (handles partial reads)
		recvRC = RecvAll(sockfd, messageBuf + sizeof(NSpMessageHeader), payloadLen);

		if (recvRC == 0)
		{
			brokenPipe = true;
			goto bye;
		}

		if (recvRC < 0)
		{
			printf("%s: error reading payload for message '%s': %d\n", 
				__func__, NSp4CCString(header->what), GetLastSocketError());
			goto bye;
		}
	}

	char* returnBuf = AllocPtr(header->messageLen);
	memcpy(returnBuf, messageBuf, header->messageLen);
	outMessage = (NSpMessageHeader*) returnBuf;
	printf("recv '%s' (%dB) #%d from P%d\n",
		NSp4CCString(outMessage->what), outMessage->messageLen, outMessage->id, outMessage->from);

bye:
	if (brokenPipe)
	{
		printf("%s: broken pipe on socket %d\n", __func__, (int) sockfd);
	}
	if (outBrokenPipe)
	{
		*outBrokenPipe = brokenPipe;
	}
	return outMessage;
}

static NSpMessageHeader* NSpMessage_GetAsHost(NSpGame* game)
{
	NSpMessageHeader* message = NULL;
	bool brokenPipe = false;

	int count = 0;
	while (count < MAX_CLIENTS && message == NULL)
	{
		int i = (game->nextPollIndex + count) % MAX_CLIENTS;
		count++;
		NSpPlayer* player = &game->players[i];

		if (!IsSocketValid(player->sockfd))
		{
			continue;
		}

		message = PollSocket(player->sockfd, &brokenPipe);

		if (brokenPipe)
		{
			GAME_ASSERT(!message);

			// Pass a fake NSpPlayerLeftMessage to application code so it can handle the client's departure
			NSpPlayerLeftMessage* leftMessage = AllocMessage(NSpPlayerLeft, kNSpHostID, kNSpHostID);
			leftMessage->playerCount		= NSpGame_GetNumActivePlayers(game) - 1;
			leftMessage->playerID			= player->id;
			CopyPlayerName(leftMessage->playerName, player->name);

			// Pass it on to application code
			message = &leftMessage->header;

			// This socket is dead now
			CloseSocket(&player->sockfd);

			// Kick the client and tell others that this guy left
			NSpPlayer_Kick(game, player->id);
		}
		else if (message)
		{
			// Round-Robin: Start next search after this player
			game->nextPollIndex = (i + 1) % MAX_CLIENTS;

			// Force client ID. The client may not know their ID yet,
			// and we don't want them to forge a bogus ID anyway.
			message->from = player->id;
			GAME_ASSERT(NSpGame_IsValidPlayerID(game, message->from));

			// Forward broadcast messages
			if (message->to == kNSpAllPlayers)
			{
				// TODO: if we ever do UDP, we'll need to decide what to do with the flags below
				NSpMessage_Send(game, message, kNSpSendFlag_Registered);
			}
		}
	}

	return message;
}

static NSpMessageHeader* NSpMessage_GetAsClient(NSpGame* game)
{
	NSpMessageHeader* message = NULL;
	bool brokenPipe = false;

	// Get the message
	message = PollSocket(game->clientToHostSocket, &brokenPipe);

	if (brokenPipe)
	{
		GAME_ASSERT(!message);

		// This socket is dead now
		CloseSocket(&game->clientToHostSocket);

		// Pass a fake NSpGameTerminatedMessage to application code so it knows to shut down the net game
		NSpGameTerminatedMessage* gameTerminated = AllocMessage(NSpGameTerminated, kNSpHostID, kNSpHostID);
		gameTerminated->reason = kNSpGameTerminated_NetworkError;
		message = &gameTerminated->header;
	}

	if (message == NULL)
	{
		return NULL;
	}

	// If we did get a message, handle internal message types
	switch (message->what)
	{
		case kNSpJoinApproved:
		{
			game->myID = message->to;		// that's our ID

			NSpPlayer* newPlayer = NSpGame_GetPlayerFromID(game, game->myID);
			GAME_ASSERT(newPlayer);

			newPlayer->id			= game->myID;
			newPlayer->state		= kNSpPlayerState_Me;
			newPlayer->sockfd		= INVALID_SOCKET;		// client has no connection to itself
			snprintf(newPlayer->name, sizeof(newPlayer->name), "YOU");		// TODO: get actual name

			break;
		}

		case kNSpPlayerJoined:
		{
			NSpPlayerJoinedMessage* joinedMessage = (NSpPlayerJoinedMessage*) message;

			NSpPlayer* newPlayer = NSpGame_GetPlayerFromID(game, joinedMessage->playerInfo.id);
			GAME_ASSERT(newPlayer);

			newPlayer->id			= joinedMessage->playerInfo.id;
			newPlayer->state		= kNSpPlayerState_ConnectedPeer;
			newPlayer->sockfd		= INVALID_SOCKET;		// client has no p2p connection to other players
			CopyPlayerName(newPlayer->name, joinedMessage->playerInfo.name);

			break;
		}

		case kNSpPlayerLeft:
		{
			NSpPlayerLeftMessage* leftMessage = (NSpPlayerLeftMessage*) message;

			NSpPlayer* deadPlayer = NSpGame_GetPlayerFromID(game, leftMessage->playerID);
			if (deadPlayer)
			{
				NSpPlayer_Clear(deadPlayer);
			}

			break;
		}
	}

	return message;
}

NSpMessageHeader* NSpMessage_Get(NSpGameReference gameRef)
{
	NSpGame* game = NSpGame_Unbox(gameRef);

	if (!game)
	{
		return NULL;
	}
	else if (game->isHosting)
	{
		return NSpMessage_GetAsHost(game);
	}
	else
	{
		return NSpMessage_GetAsClient(game);
	}
}

int NSpGame_AckJoinRequest(NSpGameReference gameRef, NSpMessageHeader* message)
{
	NSpGame* game = NSpGame_Unbox(gameRef);

	if (!game)
	{
		return kNSpRC_NoGame;
	}

	GAME_ASSERT(game->isHosting);
	GAME_ASSERT(message->what == kNSpJoinRequest);

	NSpPlayerID newPlayerID = message->from;
	NSpPlayer* newPlayer = NSpGame_GetPlayerFromID(gameRef, newPlayerID);

	if (newPlayer == NULL)
	{
		return kNSpRC_InvalidPlayer;
	}

	if (newPlayer->state != kNSpPlayerState_AwaitingHandshake)
	{
		return kNSpRC_BadState;
	}

	NSpJoinRequestMessage* joinRequestMessage = (NSpJoinRequestMessage*) message;

	// Save their name
	CopyPlayerName(newPlayer->name, joinRequestMessage->name);

	// Tell them they're in
	{
		NSpJoinApprovedMessage* approvedMessage = AllocMessage(NSpJoinApproved, kNSpHostID, newPlayerID);

		int rc = NSpMessage_Send(game, &approvedMessage->header, kNSpSendFlag_Registered);
		NSpMessage_Release(gameRef, &approvedMessage->header);

		if (rc != kNSpRC_OK)
		{
			return rc;
		}
	}

	// Send a flurry of PlayerJoined messages to the new client so it knows who joined
	for (int i = 0; i < MAX_CLIENTS; i++)
	{
		NSpPlayer* player = &game->players[i];

		if (player->state == kNSpPlayerState_Offline		// skip over dead clients
			|| player->id == newPlayerID)					// skip over itself
		{
			continue;
		}

		NSpPlayerJoinedMessage* joinedMessage = AllocMessage(NSpPlayerJoined, kNSpHostID, newPlayerID);
		joinedMessage->playerCount		= 1 + NSpGame_GetNumActivePlayers(gameRef);
		joinedMessage->playerInfo.id	= NSpGame_ClientSlotToID(gameRef, i);
		CopyPlayerName(joinedMessage->playerInfo.name, game->players[i].name);

		int rc = NSpMessage_Send(gameRef, &joinedMessage->header, kNSpSendFlag_Registered);
		NSpMessage_Release(gameRef, &joinedMessage->header);

		if (rc != kNSpRC_OK)
		{
			return rc;
		}
	}

	// We've done the initial handshake
	newPlayer->state = kNSpPlayerState_ConnectedPeer;

	// Tell peers that someone joined
	for (int i = 0; i < MAX_CLIENTS; i++)
	{
		NSpPlayer* peerPlayer = &game->players[i];

		if (peerPlayer->state != kNSpPlayerState_ConnectedPeer
			|| peerPlayer->id == newPlayer->id)						// don't send joinedMessage to the new client
		{
			continue;
		}

		NSpPlayerJoinedMessage* joinedMessage = AllocMessage(NSpPlayerJoined, kNSpHostID, peerPlayer->id);
		joinedMessage->playerCount		= NSpGame_GetNumActivePlayers(gameRef);
		joinedMessage->playerInfo.id	= newPlayerID;
		CopyPlayerName(joinedMessage->playerInfo.name, newPlayer->name);

		NSpMessage_Send(gameRef, &joinedMessage->header, kNSpSendFlag_Registered);
		NSpMessage_Release(gameRef, &joinedMessage->header);
	}

	return kNSpRC_OK;
}

void NSpMessage_Release(NSpGameReference gameRef, NSpMessageHeader* message)
{
	(void) gameRef;
	SafeDisposePtr((Ptr) message);
}

#pragma mark - Create and kill lobby

NSpGameReference NSpGame_Host(void)
{
	NSpGameReference gameRef = NULL;
	NSpGame *game = NULL;

	gameRef = NSpGame_Alloc();
	game = NSpGame_Unbox(gameRef);
	game->isHosting				= true;
	game->myID					= kNSpHostID;

	gLastQueriedSocketError = 0; // Reset stale error

	game->hostListenSocket		= CreateTCPSocket(true);

	if (!IsSocketValid(game->hostListenSocket))
	{
		SDL_Log("%s: CreateTCPSocket failed (errno=%d)", __func__, gLastQueriedSocketError);
		goto fail;
	}

	int listenRC = listen(game->hostListenSocket, PENDING_CONNECTIONS_QUEUE);

	if (listenRC != 0)
	{
		GetSocketError();
		SDL_Log("%s: listen failed: %d", __func__, gLastQueriedSocketError);
		goto fail;
	}

	// Create a player for myself
	NSpPlayer* me = NSpGame_GetPlayerFromID(game, kNSpHostID);
	if (me == NULL)
	{
		SDL_Log("%s: NSpGame_GetPlayerFromID returned NULL!", __func__);
		goto fail;
	}
	me->id			= kNSpHostID;
	me->state		= kNSpPlayerState_Me;
	me->sockfd		= INVALID_SOCKET;
	snprintf(me->name, sizeof(me->name), "HOST");

	return gameRef;

fail:
	NSpGame_Dispose(gameRef, 0);
	return NULL;
}

#pragma mark - Search for lobbies

static NSpSearch* NSpSearch_Unbox(NSpSearchReference searchRef)
{
	if (searchRef == NULL)
	{
		return NULL;
	}

	NSpSearch* searchPtr = (NSpSearch*) searchRef;

//	GAME_ASSERT(NSPGAME_COOKIE32 == gamePtr->cookie);

	return searchPtr;
}

int NSpSearch_Dispose(NSpSearchReference searchRef)
{
	NSpSearch* search = NSpSearch_Unbox(searchRef);

	if (!search)
	{
		return kNSpRC_OK;
	}

	CloseSocket(&search->listenSocket);

	SafeDisposePtr((Ptr) search);

	return kNSpRC_OK;
}

NSpSearchReference NSpSearch_StartSearchingForGameHosts(void)
{
	NSpSearch* search = (NSpSearch*) AllocPtrClear(sizeof(NSpSearch));

	search->listenSocket = CreateUDPBroadcastSocket();

	if (!IsSocketValid(search->listenSocket))
	{
		printf("%s: couldn't create socket\n", __func__);
		goto fail;
	}

	struct sockaddr_in bindAddr =
	{
		.sin_family = AF_INET,
		.sin_port = htons(gNetPort),
		.sin_addr.s_addr = INADDR_ANY,
	};

	int bindRC = bind(
		search->listenSocket,
		(struct sockaddr*) &bindAddr,
		sizeof(bindAddr));

	if (0 != bindRC)
	{
		printf("%s: bind failed: %d\n", __func__, GetSocketError());

		if (GetSocketError() == kSocketError_AddressInUse)
		{
			printf("(addr in use)\n");
		}

		goto fail;
	}

	printf("Created lobby search\n");
	return search;

fail:


	SafeDisposePtr((Ptr) search);
	return NULL;
}

static bool NSpSearch_IsHostKnown(NSpSearchReference searchRef, const struct sockaddr_in* remoteAddr)
{
	NSpSearch* search = NSpSearch_Unbox(searchRef);

	if (!search)
	{
		return false;
	}

	for (int i = 0; i < search->numGamesFound; i++)
	{
		if (search->gamesFound[i].hostAddr.sin_addr.s_addr == remoteAddr->sin_addr.s_addr
			&& search->gamesFound[i].hostAddr.sin_port == remoteAddr->sin_port)
		{
			return true;
		}
	}

	return false;
}

int NSpSearch_Tick(NSpSearchReference searchRef)
{
	NSpSearch* search = NSpSearch_Unbox(searchRef);

	if (!search)
	{
		return kNSpRC_NoSearch;
	}

	if (!IsSocketValid(search->listenSocket))
	{
		return kNSpRC_InvalidSocket;
	}

	char message[kNSpMaxMessageLength];
	struct sockaddr_in remoteAddr;
	socklen_t remoteAddrLen = sizeof(remoteAddr);

	ssize_t receivedBytes = recvfrom(
		search->listenSocket,
		message,
		sizeof(message),
		0,
		(struct sockaddr*) &remoteAddr,
		&remoteAddrLen
	);

	if (receivedBytes == -1)
	{
		if (GetSocketError() == kSocketError_WouldBlock)
		{
			return kNSpRC_OK;
		}
		else
		{
			printf("%s: error %d\n", __func__, GetSocketError());
			return kNSpRC_RecvFailed;
		}
	}
	else
	{
		// TODO: Check payload!!

		if (search->numGamesFound < MAX_LOBBIES &&
			!NSpSearch_IsHostKnown(search, &remoteAddr))
		{
			char hostname[128];
			snprintf(hostname, sizeof(hostname), "[EMPTY]");
			inet_ntop(remoteAddr.sin_family, &remoteAddr.sin_addr, hostname, sizeof(hostname));
			printf("%s: Found a game! %s:%d\n", __func__, hostname, remoteAddr.sin_port);

			search->gamesFound[search->numGamesFound].hostAddr = remoteAddr;

			search->numGamesFound++;

			GAME_ASSERT(search->numGamesFound <= MAX_LOBBIES);
		}

		return kNSpRC_OK;
	}
}

int NSpSearch_GetNumGamesFound(NSpSearchReference searchRef)
{
	NSpSearch* search = NSpSearch_Unbox(searchRef);

	if (!search)
	{
		return 0;
	}

	return search->numGamesFound;
}

NSpGameReference NSpSearch_JoinGame(NSpSearchReference searchRef, int lobbyNum)
{
	NSpSearch* search = NSpSearch_Unbox(searchRef);

	if (!search)
	{
		return NULL;
	}

	GAME_ASSERT(lobbyNum >= 0);
	GAME_ASSERT(lobbyNum < search->numGamesFound);
	return JoinLobby(&search->gamesFound[lobbyNum]);
}

const char* NSpSearch_GetHostAddress(NSpSearchReference searchRef, int lobbyNum)
{
	NSpSearch* search = NSpSearch_Unbox(searchRef);

	if (!search)
	{
		return NULL;
	}

	GAME_ASSERT(lobbyNum >= 0);
	GAME_ASSERT(lobbyNum < search->numGamesFound);
	return FormatAddress(search->gamesFound[lobbyNum].hostAddr);
}

#pragma mark - NSpGame

static NSpGameReference NSpGame_Alloc(void)
{
	NSpGame* game = AllocPtrClear(sizeof(NSpGame));
	game = (NSpGameReference) game;

	game->hostListenSocket		= INVALID_SOCKET;
	game->hostAdvertiseSocket	= INVALID_SOCKET;
	game->clientToHostSocket	= INVALID_SOCKET;
	game->isHosting				= false;
	game->timeToReadvertise		= 0;
	game->myID					= kNSpUnspecifiedEndpoint;
	game->cookie				= NSPGAME_COOKIE32;

	for (int i = 0; i < MAX_CLIENTS; i++)
	{
		NSpPlayer_Clear(&game->players[i]);
	}

	return (NSpGameReference) game;
}

static NSpGame* NSpGame_Unbox(NSpGameReference gameRef)
{
	if (gameRef == NULL)
	{
		return NULL;
	}

	NSpGame* gamePtr = (NSpGame*) gameRef;

	GAME_ASSERT(NSPGAME_COOKIE32 == gamePtr->cookie);

	return gamePtr;
}

static void NSpGame_WaitForClientsToCloseSockets(NSpGame* game)
{
	const int retryDelay = 25;
	const int maxDelay = 1000;

	// 10 tries, 50 ms delay => wait at most half a second for everyone to close
	for (int tries = 0; tries < maxDelay/retryDelay; tries++)
	{
		int numSocketsToClose = MAX_CLIENTS;

		for (int i = 0; i < MAX_CLIENTS; i++)
		{
			NSpPlayer* client = &game->players[i];

			if (IsSocketValid(client->sockfd))
			{
				bool brokenPipe = false;
				NSpMessageHeader* junk = PollSocket(client->sockfd, &brokenPipe);
				if (brokenPipe)
				{
					CloseSocket(&client->sockfd);
					numSocketsToClose--;
				}
				if (junk)
				{
					NSpMessage_Release(game, junk);
				}
			}
			else
			{
				numSocketsToClose--;
			}
		}

		if (numSocketsToClose == 0)
		{
			break;
		}
		else
		{
			SDL_Delay(retryDelay);
		}
	}
}

int NSpGame_Dispose(NSpGameReference inGame, int disposeFlags)
{
	NSpGame* game = NSpGame_Unbox(inGame);

	if (!game)
	{
		return kNSpRC_NoGame;
	}

	// If we're terminating the game, tell people about it.
	if (disposeFlags & kNSpGameFlag_ForceTerminateGame)
	{
		NSpGameTerminatedMessage* byeMessage = AllocMessage(NSpGameTerminated, kNSpHostID, kNSpAllPlayers);
		byeMessage->reason = kNSpGameTerminated_HostBailed;
		NSpMessage_Send(inGame, &byeMessage->header, kNSpSendFlag_Registered);
		NSpMessage_Release(inGame, &byeMessage->header);
	}

	CloseSocket(&game->clientToHostSocket);
	CloseSocket(&game->hostListenSocket);
	CloseSocket(&game->hostAdvertiseSocket);

	// Prevent dangling TIME-WAIT sockets (which may cause EADDRINUSE if we try to host again):
	// wait for the clients to receive the bye message and close the socket on their end.
	NSpGame_WaitForClientsToCloseSockets(game);

	// Erase clients
	for (int i = 0; i < MAX_CLIENTS; i++)
	{
		NSpPlayer_Clear(&game->players[i]);
	}

	game->cookie = 'DEAD';
	SafeDisposePtr((Ptr) game);

	return kNSpRC_OK;
}

int NSpGame_GetNumActivePlayers(NSpGameReference gameRef)
{
	NSpGame* game = NSpGame_Unbox(gameRef);

	if (!game)
	{
		return 0;
	}

	int count = 0;

	for (int i = 0; i < MAX_CLIENTS; i++)
	{
		switch (game->players[i].state)
		{
			case kNSpPlayerState_Me:
			case kNSpPlayerState_ConnectedPeer:
				count++;
				break;

			default:
				break;
		}
	}

	return count;
}

uint32_t NSpGame_GetActivePlayersIDMask(NSpGameReference gameRef)
{
	NSpGame* game = NSpGame_Unbox(gameRef);

	if (!game)
	{
		return 0;
	}

	uint32_t mask = 0;

	for (int i = 0; i < MAX_CLIENTS; i++)
	{
		switch (game->players[i].state)
		{
			case kNSpPlayerState_Me:
			case kNSpPlayerState_ConnectedPeer:
				mask |= 1 << game->players[i].id;
				break;

			default:
				break;
		}
	}

	return mask;
}

bool NSpGame_IsValidPlayerID(NSpGameReference gameRef, NSpPlayerID id)
{
	if (id < kNSpClientID0 || id >= kNSpClientID0 + MAX_CLIENTS)
	{
		return false;
	}

	// TODO: Check that it's live?
	return true;
}

static NSpPlayerID NSpGame_ClientSlotToID(NSpGameReference gameRef, int slot)
{
	if (slot < 0 || slot >= MAX_CLIENTS)
	{
		return kNSpUnspecifiedEndpoint;
	}
	else
	{
		return slot + kNSpHostID;
	}
}

static int NSpGame_ClientIDToSlot(NSpGameReference gameRef, NSpPlayerID id)
{
	if (!NSpGame_IsValidPlayerID(gameRef, id))
	{
		return -1;
	}
	else
	{
		return id - kNSpHostID;
	}
}

static NSpPlayer* NSpGame_GetPlayerFromID(NSpGameReference gameRef, NSpPlayerID id)
{
	NSpGame* game = NSpGame_Unbox(gameRef);

	if (!game)
	{
		return NULL;
	}

	int slot = NSpGame_ClientIDToSlot(gameRef, id);

	if (slot >= 0)
	{
		return &game->players[id];
	}
	else
	{
		return NULL;
	}
}

NSpPlayerID NSpGame_GetNthActivePlayerID(NSpGameReference gameRef, int n)
{
	NSpGame* game = NSpGame_Unbox(gameRef);

	if (!game)
	{
		return kNSpUnspecifiedEndpoint;
	}

	for (int i = 0; i < MAX_CLIENTS; i++)
	{
		NSpPlayer* player = &game->players[i];

		if (player->state == kNSpPlayerState_Offline
			|| player->state == kNSpPlayerState_AwaitingHandshake)
		{
			continue;
		}

		if (n == 0)
		{
			return player->id;
		}
		else
		{
			n--;
		}
	}

	return kNSpUnspecifiedEndpoint;
}

bool NSpGame_IsAdvertising(NSpGameReference gameRef)
{
	NSpGame* game = NSpGame_Unbox(gameRef);

	return game && game->isHosting && game->isAdvertising;
}

int NSpGame_StartAdvertising(NSpGameReference gameRef)
{
	NSpGame* game = NSpGame_Unbox(gameRef);

	if (!game)
	{
		return kNSpRC_NoGame;
	}

	if (!game->isHosting)
	{
		return kNSpRC_BadState;
	}

	if (game->isAdvertising)
	{
		return kNSpRC_OK;
	}

	game->hostAdvertiseSocket = CreateUDPBroadcastSocket();

	if (!IsSocketValid(game->hostAdvertiseSocket))
	{
		return kNSpRC_InvalidSocket;
	}

	game->isAdvertising = true;
	game->timeToReadvertise = 0;
	return kNSpRC_OK;
}

int NSpGame_StopAdvertising(NSpGameReference gameRef)
{
	NSpGame* game = NSpGame_Unbox(gameRef);

	if (!game)
	{
		return kNSpRC_NoGame;
	}

	if (!game->isHosting)
	{
		return kNSpRC_BadState;
	}

	if (!game->isAdvertising)
	{
		return kNSpRC_OK;
	}

	CloseSocket(&game->hostAdvertiseSocket);
	game->isAdvertising = false;
	game->timeToReadvertise = 0;

	return kNSpRC_OK;
}

int NSpGame_AdvertiseTick(NSpGameReference gameRef, float dt)
{
	NSpGame* game = NSpGame_Unbox(gameRef);

	if (!game)
	{
		return kNSpRC_NoGame;
	}

	if (!game->isHosting ||
		!game->isAdvertising)
	{
		return kNSpRC_BadState;
	}

	if (!IsSocketValid(game->hostAdvertiseSocket))
	{
		return kNSpRC_InvalidSocket;
	}

	game->timeToReadvertise -= dt;

	if (game->timeToReadvertise > 0)
	{
		return kNSpRC_OK;
	}

	// Fire the message

	game->timeToReadvertise = LOBBY_BROADCAST_INTERVAL;

	printf("%s: broadcasting message\n", __func__);

	const char* message = "JOIN MY CMR GAME";
	size_t messageLength = strlen(message);

	struct sockaddr_in broadcastAddr =
	{
		.sin_family = AF_INET,
		.sin_port = htons(gNetPort),
		.sin_addr.s_addr = INADDR_BROADCAST,
	};

	ssize_t rc = sendto(
		game->hostAdvertiseSocket,
		message,
		messageLength,
		MSG_NOSIGNAL,
		(struct sockaddr*) &broadcastAddr,
		sizeof(broadcastAddr)
	);

	if (rc == -1)
	{
		printf("%s: sendto(%d) : error % d\n",
			__func__, (int) game->hostAdvertiseSocket, GetSocketError());
		return kNSpRC_SendFailed;
	}

	return kNSpRC_OK;
}

#pragma mark - NSpMessage

static int SendOnSocket(sockfd_t sockfd, NSpMessageHeader* header)
{
	GAME_ASSERT_MESSAGE(header->what != kNSpUndefinedMessage, "Did you forget to set header->what?");
	GAME_ASSERT_MESSAGE(header->messageLen != 0xBADBABEE, "Did you forget to set header->messageLen?");
	GAME_ASSERT(header->messageLen >= sizeof(NSpMessageHeader));
	GAME_ASSERT(header->messageLen <= kNSpMaxMessageLength);
	GAME_ASSERT(header->version == kNSpCMRProtocol4CC);

	if (!IsSocketValid(sockfd))
	{
		printf("%s: invalid socket %d\n", __func__, (int) sockfd);
		return kNSpRC_InvalidSocket;
	}

	// Retry loop for EWOULDBLOCK - important for slow WiFi clients where
	// the send buffer may fill up temporarily
	const int maxRetries = 10;
	const int retryDelayMs = 10;

	for (int attempt = 0; attempt < maxRetries; attempt++)
	{
		ssize_t sendRC = send(
			sockfd,
			(char*) header,
			header->messageLen,
			MSG_NOSIGNAL
		);

		if (sendRC >= 0)
		{
			printf("send '%s' (%dB) #%d -> %d\n",
				NSp4CCString(header->what), header->messageLen, header->id, (int) sockfd);
			return kNSpRC_OK;
		}

		int err = GetSocketError();
		if (err == kSocketError_WouldBlock)
		{
			// Buffer full, wait briefly and retry
			if (attempt < maxRetries - 1)
			{
				SDL_Delay(retryDelayMs);
				continue;
			}
			// Fall through to failure after max retries
		}

		// Non-retryable error or max retries exceeded
		break;
	}

	printf("%s: error sending message on socket %d after retries\n", __func__, (int) sockfd);
	return kNSpRC_SendFailed;
}

// Attempts to send a message.
// If we're the host and sending fails, automatically kicks the client.
// Kicking the client will in turn broadcast a PlayerLeftMessage to remaining clients,
// if the other clients already know the kicked client.
int NSpMessage_Send(NSpGameReference gameRef, NSpMessageHeader* header, int flags)
{
	NSpGame* game = NSpGame_Unbox(gameRef);

	if (!game)
	{
		return kNSpRC_NoGame;
	}

	if (header->from == kNSpUnspecifiedEndpoint)
	{
		header->from = game->myID;
	}

	bool kickOnFail = !(flags & kNSpSendFlag_DontKickOnFail);

	GAME_ASSERT_MESSAGE(flags & kNSpSendFlag_Registered, "only reliable messages are supported");

	if (game->isHosting)
	{
		switch (header->to)
		{
			case kNSpAllPlayers:
			{
				int anyError = 0;
				for (int i = 0; i < MAX_CLIENTS; i++)
				{
					NSpPlayer* peer = &game->players[i];

					if (peer->state == kNSpPlayerState_ConnectedPeer	// send to peers with complete handshake
						&& peer->id != header->from)					// don't return to sender!
					{
						int rc = SendOnSocket(game->players[i].sockfd, header);
						if (rc != kNSpRC_OK && kickOnFail)
						{
							anyError = rc;
							NSpPlayerID kickedPlayerID = NSpGame_ClientSlotToID(gameRef, i);
							NSpPlayer_Kick(gameRef, kickedPlayerID);
						}
					}
				}
				return anyError;
			}

			case kNSpHostID:
				GAME_ASSERT_MESSAGE(false, "Host cannot send itself a message");
				break;

			default:
			{
				NSpPlayerID playerID = header->to;
				NSpPlayer* peer = NSpGame_GetPlayerFromID(gameRef, playerID);

				if (peer
					&& (peer->state == kNSpPlayerState_ConnectedPeer
						|| peer->state == kNSpPlayerState_AwaitingHandshake))
				{
					int rc = SendOnSocket(peer->sockfd, header);
					if (rc != kNSpRC_OK && kickOnFail)
					{
						NSpPlayer_Kick(gameRef, playerID);
					}
					return rc;
				}
				else
				{
					return kNSpRC_InvalidPlayer;
				}
			}
		}
	}
	else
	{
		// If we're a client, forward ALL messages to the host.
		// The host will dispatch the message for us.

		int rc = SendOnSocket(game->clientToHostSocket, header);
		if (rc != kNSpRC_OK)
		{
			// TODO: Client couldn't send a message to the host! Kill the game?
			puts("Client couldn't send a message to the host! Kill the game?");
		}

		return rc;
	}
}

#pragma mark - NSpCMRClient

int NSpPlayer_Kick(NSpGameReference gameRef, NSpPlayerID kickedPlayerID)
{
	char playerNameBackup[kNSpPlayerNameLength];

	NSpGame* game = NSpGame_Unbox(gameRef);

	if (!game)
	{
		return kNSpRC_NoGame;
	}

	GAME_ASSERT(game->isHosting);
	GAME_ASSERT(kickedPlayerID != kNSpHostID);		// can't kick myself!

	NSpPlayer* kickedPlayer = NSpGame_GetPlayerFromID(gameRef, kickedPlayerID);
	if (kickedPlayer == NULL)
	{
		return kNSpRC_InvalidPlayer;
	}

	// If we did the initial handshake, we've already let the others know about this peer.
	// So we'll need to tell them that this one left.
	bool tellOthers = kickedPlayer->state == kNSpPlayerState_ConnectedPeer;

	// Send this peer an NSpGameTerminatedMessage if their socket is still live
	if (IsSocketValid(kickedPlayer->sockfd))
	{
		NSpGameTerminatedMessage* byeMessage = AllocMessage(NSpGameTerminated, kNSpHostID, kickedPlayer->id);
		byeMessage->reason = kNSpGameTerminated_YouGotKicked;

		NSpMessage_Send(gameRef, &byeMessage->header, kNSpSendFlag_Registered | kNSpSendFlag_DontKickOnFail);
		NSpMessage_Release(gameRef, &byeMessage->header);
	}

	CopyPlayerName(playerNameBackup, kickedPlayer->name);

	CloseSocket(&kickedPlayer->sockfd);
	NSpPlayer_Clear(kickedPlayer);
	kickedPlayer = NULL;

	// Tell other players that this guy left
	if (tellOthers)
	{
		for (int i = 0; i < MAX_CLIENTS; i++)
		{
			if (!IsSocketValid(game->players[i].sockfd))
			{
				continue;
			}

			NSpPlayerLeftMessage* leftMessage = AllocMessage(NSpPlayerLeft, kNSpHostID, NSpGame_ClientSlotToID(gameRef, i));
			leftMessage->playerCount = NSpGame_GetNumActivePlayers(gameRef);
			leftMessage->playerID = kickedPlayerID;
			CopyPlayerName(leftMessage->playerName, playerNameBackup);

			NSpMessage_Send(gameRef, &leftMessage->header, kNSpSendFlag_Registered);
			NSpMessage_Release(gameRef, &leftMessage->header);
		}
	}

	return kNSpRC_OK;
}

const char* NSpPlayer_GetName(NSpGameReference gameRef, NSpPlayerID playerID)
{
	NSpPlayer* player = NSpGame_GetPlayerFromID(gameRef, playerID);

	return player == NULL ? NULL : player->name;
}

// Does NOT close the socket!
static void NSpPlayer_Clear(NSpPlayer* player)
{
	memset(player, 0, sizeof(NSpPlayer));
	player->id			= kNSpUnspecifiedEndpoint;
	player->sockfd		= INVALID_SOCKET;
	player->state		= kNSpPlayerState_Offline;
}

NSpPlayerID NSpPlayer_GetMyID(NSpGameReference gameRef)
{
	NSpGame* game = NSpGame_Unbox(gameRef);

	if (!game)
	{
		return kNSpUnspecifiedEndpoint;
	}

	return game->myID;
}
