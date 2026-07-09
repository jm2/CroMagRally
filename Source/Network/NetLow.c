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
#if __APPLE__
#include <ifaddrs.h>
#endif
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
#include "net_validation.h"

int gNetPort = 49959;
#define LOBBY_BROADCAST_INTERVAL 1.0f

#define PENDING_CONNECTIONS_QUEUE 10

#define MAX_LOBBIES 5

#define NSPGAME_COOKIE32 'NSpG'

#if _WIN32
#define kSocketError_WouldBlock			WSAEWOULDBLOCK
#define kSocketError_InProgress			WSAEINPROGRESS
#define kSocketError_AlreadyConnected	WSAEISCONN
#define kSocketError_AddressInUse		WSAEADDRINUSE
#else
#define kSocketError_WouldBlock			EAGAIN
#define kSocketError_InProgress			EINPROGRESS
#define kSocketError_AlreadyConnected	EISCONN
#define kSocketError_AddressInUse		EADDRINUSE
#endif

// Socket buffer sizes for burst absorption
#define SOCKET_SNDBUF_SIZE 65536
#define SOCKET_RCVBUF_SIZE 65536

// Per-socket non-blocking send ring (Stage 1). Absorbs whatever the kernel send buffer
// can't take in one shot so the main thread never blocks retrying a slow/stalled peer.
// 32 KB ~= 2s of host control msgs (~290B @60pps) or ~10s of client msgs (~52B); a single
// max message (kNSpMaxMessageLength) always fits an empty ring, so overflow only ever comes
// from sustained backlog -> the existing kick (host) / terminate (client) path.
#define SEND_RING_CAPACITY 32768

typedef struct SendRing
{
	uint32_t					head;			// read cursor: next byte to send()
	uint32_t					tail;			// write cursor: next byte to append
	uint32_t					used;			// bytes currently queued (0..SEND_RING_CAPACITY)
	uint32_t					highWater;		// telemetry: max 'used' ever seen
	uint8_t						data[SEND_RING_CAPACITY];
} SendRing;

static void		SendRing_Reset(SendRing* r);
static uint32_t	SendRing_FreeSpace(const SendRing* r);
static bool		SendRing_Append(SendRing* r, const uint8_t* src, uint32_t len);
static int		SendRing_Drain(SendRing* r, sockfd_t sockfd);

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
	SendRing					sendRing;		// host-side outbound queue for this peer socket (auto-reset by NSpPlayer_Clear's memset)
	bool						needsLeaveNotify;	// host: a send to this peer failed; NSpMessage_GetAsHost owes the host's own NetHigh a synthetic kNSpPlayerLeft before the slot is reused
	uint32_t					lastHeard;		// CMR7 Stage 4: host per-client last-received-bytes time (ms, SDL_GetTicks); drives the badge/drop policy
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

	SendRing					clientSendRing;	// client-side outbound queue for clientToHostSocket (zero-init by AllocPtrClear in NSpGame_Alloc)

	uint32_t					hostLastHeard;	// CMR7 Stage 4: client tracks last-received-bytes time from host (ms, SDL_GetTicks)
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
static void NSpPlayer_DeferLeaveNotify(NSpPlayer* peer);
static int SendBytesBlocking(sockfd_t sockfd, NSpMessageHeader* header);
static int SendOrEnqueue(sockfd_t sockfd, SendRing* ring, NSpMessageHeader* header);
static bool ConnectWithTimeout(sockfd_t sockfd, const struct sockaddr* addr, socklen_t addrLen, uint32_t timeoutMs);

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

	sockfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (!IsSocketValid(sockfd))
	{
		printf("%s: socket failed: %d\n", __func__, GetSocketError());
		goto fail;
	}

	int broadcast = 1;
	int sockoptRC = setsockopt(sockfd, SOL_SOCKET, SO_BROADCAST, (void*) &broadcast, sizeof(broadcast));
	if (-1 == sockoptRC)
	{
		goto fail;
	}

	if (!MakeSocketNonBlocking(sockfd))
	{
		goto fail;
	}

	return sockfd;

fail:
	CloseSocket(&sockfd);
	return sockfd;
}

#if __IOS__ || __TVOS__
// iOS 14.4+ blocks UDP broadcasts to INADDR_BROADCAST (255.255.255.255)
// without the com.apple.developer.networking.multicast entitlement.
// Compute subnet-directed broadcast address instead (e.g., 192.168.1.255).
static struct in_addr GetSubnetBroadcastAddress(void)
{
	struct in_addr broadcastAddr = { .s_addr = INADDR_BROADCAST };
	struct ifaddrs *iflist, *ifa;

	if (getifaddrs(&iflist) != 0)
		return broadcastAddr;

	// Two-pass approach: first look for WiFi/Ethernet, then fall back to other interfaces
	// en0 = WiFi, en1/en2/en3 = USB/Thunderbolt Ethernet adapters
	const char* preferredInterfaces[] = { "en0", "en1", "en2", "en3", NULL };
	
	for (int pass = 0; pass < 2; pass++)
	{
		for (ifa = iflist; ifa != NULL; ifa = ifa->ifa_next)
		{
			if (ifa->ifa_addr == NULL || ifa->ifa_addr->sa_family != AF_INET)
				continue;

			// Skip loopback
			if (strcmp(ifa->ifa_name, "lo0") == 0)
				continue;

			// Skip cellular interfaces (pdp_ip*)
			if (strncmp(ifa->ifa_name, "pdp_ip", 6) == 0)
				continue;

			// Pass 0: only accept preferred interfaces
			// Pass 1: accept any remaining interface
			if (pass == 0)
			{
				bool isPreferred = false;
				for (int i = 0; preferredInterfaces[i] != NULL; i++)
				{
					if (strcmp(ifa->ifa_name, preferredInterfaces[i]) == 0)
					{
						isPreferred = true;
						break;
					}
				}
				if (!isPreferred)
					continue;
			}

			// Get IP and netmask
			struct sockaddr_in *addr = (struct sockaddr_in *)ifa->ifa_addr;
			struct sockaddr_in *netmask = (struct sockaddr_in *)ifa->ifa_netmask;

			if (addr->sin_addr.s_addr == INADDR_ANY)
				continue;

			// Compute broadcast = IP | ~netmask
			broadcastAddr.s_addr = addr->sin_addr.s_addr | ~netmask->sin_addr.s_addr;
			printf("Using subnet broadcast: %s (from %s)\n",
				inet_ntoa(broadcastAddr), ifa->ifa_name);
			freeifaddrs(iflist);
			return broadcastAddr;
		}
	}

	freeifaddrs(iflist);
	return broadcastAddr;
}
#endif


#pragma mark - Join lobby

static bool ConnectWithTimeout(sockfd_t sockfd, const struct sockaddr* addr, socklen_t addrLen, uint32_t timeoutMs)
{
	if (connect(sockfd, addr, addrLen) == 0)
		return true;

	int err = GetSocketError();
	if (err != kSocketError_WouldBlock && err != kSocketError_InProgress)
		return err == kSocketError_AlreadyConnected;

	uint64_t deadline = SDL_GetTicks() + timeoutMs;
	while (SDL_GetTicks() < deadline)
	{
		fd_set writeSet;
		fd_set errorSet;
		FD_ZERO(&writeSet);
		FD_ZERO(&errorSet);
		FD_SET(sockfd, &writeSet);
		FD_SET(sockfd, &errorSet);

		struct timeval timeout = {.tv_sec = 0, .tv_usec = 50000};
		int ready = select((int) sockfd + 1, NULL, &writeSet, &errorSet, &timeout);
		if (ready > 0)
		{
			int socketError = 0;
			socklen_t socketErrorLen = sizeof(socketError);
			if (getsockopt(sockfd, SOL_SOCKET, SO_ERROR, (void*) &socketError, &socketErrorLen) != 0)
				return false;
			return socketError == 0 || socketError == kSocketError_AlreadyConnected;
		}
		if (ready < 0)
			return false;

		DoSDLMaintenance();
	}

	printf("%s: connection timed out after %u ms\n", __func__, timeoutMs);
	return false;
}

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

	if (!ConnectWithTimeout(sockfd, (const struct sockaddr*) &bindAddr, sizeof(bindAddr), 3000))
	{
		printf("%s: connect failed: %d\n", __func__, GetSocketError());
		goto fail;
	}

	NSpJoinRequestMessage* joinRequestMessage = AllocMessage(NSpJoinRequest, kNSpUnspecifiedEndpoint, kNSpHostID);
	snprintf(joinRequestMessage->name, sizeof(joinRequestMessage->name), "CLIENT");

	int rc = SendBytesBlocking(sockfd, &joinRequestMessage->header);	// one-shot lobby send: no per-socket ring exists yet
	NSpMessage_Release(NULL, &joinRequestMessage->header);

	if (rc != kNSpRC_OK)
	{
		goto fail;
	}

	NSpGameReference gameRef = NSpGame_Alloc();
	NSpGame* game = NSpGame_Unbox(gameRef);
	game->isHosting = false;
	game->clientToHostSocket = sockfd;
	game->hostLastHeard = (uint32_t) SDL_GetTicks();	// CMR7 Stage 4: seed host-link liveness at join time
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
	for (int i = 0; i < MAX_CLIENTS; i++)		// players[] is sized MAX_CLIENTS, not MAX_PLAYERS — iterating to MAX_PLAYERS read/wrote past the array
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
		newPlayer->lastHeard	= (uint32_t) SDL_GetTicks();	// CMR7 Stage 4: seed liveness so a fresh slot isn't instantly "stale"
		snprintf(newPlayer->name, sizeof(newPlayer->name), "PLAYER %d", newPlayer->id);

		SendRing_Reset(&newPlayer->sendRing);	// start a reused slot with an empty ring (defensive; NSpPlayer_Clear already zeroed it on the prior kick)
	}
	else
	{
		// All slots used up
		printf("%s: A new client wants to connect, but the game is full!\n", __func__);

		NSpJoinDeniedMessage* deniedMessage = AllocMessage(NSpJoinDenied, kNSpHostID, kNSpUnspecifiedEndpoint);
		snprintf(deniedMessage->reason, sizeof(deniedMessage->reason), "THE GAME IS FULL.");

		SendBytesBlocking(newSocket, &deniedMessage->header);	// one-shot denial; socket is closed immediately after
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
	sockfd_t sockfd = INVALID_SOCKET;
	struct addrinfo* res = NULL;

	struct addrinfo hints =
	{
		.ai_family = AF_INET,
		.ai_socktype = SOCK_STREAM,
		.ai_flags = AI_PASSIVE,
		.ai_protocol = IPPROTO_TCP,
	};

	char portStr[16];
	snprintf(portStr, sizeof(portStr), "%d", gNetPort);
	if (0 != getaddrinfo(NULL, portStr, &hints, &res))
	{
		goto fail;
	}

	struct addrinfo* goodRes = res;  // TODO: actually walk through res

	sockfd = socket(goodRes->ai_family, goodRes->ai_socktype, goodRes->ai_protocol);
	if (!IsSocketValid(sockfd))
	{
		goto fail;
	}

	if (!MakeSocketNonBlocking(sockfd))
	{
		goto fail;
	}

	// Apply all performance/robustness socket options
	ApplyTCPSocketOptions(sockfd);

	if (bindIt)
	{
		// Allow re-hosting on the same port without waiting out TIME-WAIT or a leaked socket
		// from a previous game (otherwise a second host attempt fails with EADDRINUSE).
		int reuse = 1;
		setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, (void *)&reuse, sizeof(reuse));

		int bindRC = bind(sockfd, goodRes->ai_addr, goodRes->ai_addrlen);
		if (0 != bindRC)
		{
			printf("%s: bind failed: %d\n", __func__, GetSocketError());
			goto fail;
		}
	}

	freeaddrinfo(res);
	res = NULL;
	return sockfd;

fail:
	if (res != NULL)
	{
		freeaddrinfo(res);
		res = NULL;
	}
	CloseSocket(&sockfd);
	return INVALID_SOCKET;
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

	// if < 0, check for WouldBlock. If so, return NULL (no data yet).
	if (recvRC < 0)
	{
		int err = GetSocketError();
		if (err != kSocketError_WouldBlock)
		{
			printf("%s: error peeking header: %d\n", __func__, err);
			// For non-blocking/poll, read errors other than EWOULDBLOCK are usually fatal
			brokenPipe = true; 
		}
		goto bye;
	}

	// If we got *some* data but not a full header, we must WAIT (return NULL)
	// Do NOT consume it yet, or we risked blocking in RecvAll.
	if (recvRC < (ssize_t)sizeof(NSpMessageHeader))
	{
		// Not enough for a full header yet. Return and try again next frame.
		goto bye;
	}

	// If we are here, we have at least a full header in the buffer.
	// We can safely peek at it to know the message length.
	NSpMessageHeader* peekHeader = (NSpMessageHeader*) messageBuf;

	if (peekHeader->version != kNSpCMRProtocol4CC)
	{
		printf("%s: bad protocol %08x\n", __func__, peekHeader->version);
		brokenPipe = true; // Garbage on line? Close it.
		goto bye;
	}

	if (peekHeader->messageLen > kNSpMaxMessageLength
		|| peekHeader->messageLen < sizeof(NSpMessageHeader))
	{
		printf("%s: invalid message length %u\n", __func__, peekHeader->messageLen);
		brokenPipe = true;
		goto bye;
	}

	// Now check if we have the FULL message (Header + Payload) available
	// doing a second PEEK for the full length.
	size_t fullMsgLen = peekHeader->messageLen;
	if (fullMsgLen > sizeof(NSpMessageHeader))
	{
		recvRC = recv(
			sockfd,
			messageBuf,
			fullMsgLen,
			MSG_NOSIGNAL | MSG_PEEK
		);

		if (recvRC < (ssize_t)fullMsgLen)
		{
			// Not enough for the full payload yet. Return and try again.
			goto bye;
		}
	}

	// OK, we have the full message available in the socket buffer.
	// Now we can consume it safely with RecvAll (it won't block).
	recvRC = RecvAll(sockfd, messageBuf, fullMsgLen);
	if (recvRC <= 0)
	{
		// Should not happen since we just peeked it, but handle error just in case
		brokenPipe = true; 
		goto bye;
	}

	char* returnBuf = AllocPtr(fullMsgLen);
	memcpy(returnBuf, messageBuf, fullMsgLen);
	outMessage = (NSpMessageHeader*) returnBuf;

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

		// A host-side SEND failure (ring overflow / dead socket) flagged this slot but could not
		// notify the host's own NetHigh from the send path. Synthesize the local kNSpPlayerLeft
		// HERE — the same logical point the recv-side broken-pipe branch below does — so the host
		// runs PlayerUnexpectedlyLeavesGame in lockstep with the remote clients (keeps isComputer /
		// gNumGatheredPlayers / the synced-RNG draw count consistent, avoiding a seed desync). This
		// must come before the IsSocketValid skip because the send path already closed the socket.
		if (player->needsLeaveNotify)
		{
			player->needsLeaveNotify = false;

			NSpPlayerLeftMessage* leftMessage = AllocMessage(NSpPlayerLeft, kNSpHostID, kNSpHostID);
			leftMessage->playerCount		= NSpGame_GetNumActivePlayers(game) - 1;
			leftMessage->playerID			= player->id;
			CopyPlayerName(leftMessage->playerName, player->name);
			message = &leftMessage->header;

			// Round-Robin: start next search after this player
			game->nextPollIndex = (i + 1) % MAX_CLIENTS;

			// Tell the OTHER clients this guy left + clear the slot (socket already closed)
			NSpPlayer_Kick(game, player->id);

			continue;	// message != NULL -> loop exits with the synthesized leave
		}

		if (!IsSocketValid(player->sockfd))
		{
			continue;
		}

			message = PollSocket(player->sockfd, &brokenPipe);

			if (message
				&& (!NetValidateInboundEnvelope(kNetInbound_Host, message)
					|| (player->state == kNSpPlayerState_AwaitingHandshake && message->what != kNSpJoinRequest)
					|| (player->state == kNSpPlayerState_ConnectedPeer && message->what == kNSpJoinRequest)))
			{
				printf("%s: protocol violation from player %d: type=%s len=%u\n",
					__func__, (int) player->id, NSp4CCString(message->what), message->messageLen);
				NSpMessage_Release(game, message);
				message = NULL;
				brokenPipe = true;
			}

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
			// CMR7 Stage 4: any byte from this client refreshes its liveness clock (covers 'keep' + all real traffic).
			player->lastHeard = (uint32_t) SDL_GetTicks();

			// Round-Robin: Start next search after this player
			game->nextPollIndex = (i + 1) % MAX_CLIENTS;

			// Force client ID. The client may not know their ID yet,
			// and we don't want them to forge a bogus ID anyway.
			message->from = player->id;
			GAME_ASSERT(NSpGame_IsValidPlayerID(game, message->from));

				// Application broadcasts are relayed by NetHigh only after semantic validation.
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

	if (message && !NetValidateInboundEnvelope(kNetInbound_Client, message))
	{
		printf("%s: protocol violation from host: type=%s len=%u\n",
			__func__, NSp4CCString(message->what), message->messageLen);
		NSpMessage_Release(game, message);
		message = NULL;
		brokenPipe = true;
	}

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

	// CMR7 Stage 4: real bytes from the host refresh the host-link liveness clock (covers 'keep' + all
	// traffic). A synthesized broken-pipe GameTerminated must NOT refresh it — the host is gone.
	if (!brokenPipe)
	{
		game->hostLastHeard = (uint32_t) SDL_GetTicks();
	}

	// If we did get a message, handle internal message types
	switch (message->what)
	{
		case kNSpJoinApproved:
		{
			game->myID = message->to;		// that's our ID

			NSpPlayer* newPlayer = NSpGame_GetPlayerFromID(game, game->myID);
			if (!newPlayer)					// host supplied an out-of-range id: ignore rather than assert-crash
			{
				printf("kNSpJoinApproved: invalid id %d from host; ignoring.\n", (int) game->myID);
				break;
			}

			newPlayer->id			= game->myID;
			newPlayer->state		= kNSpPlayerState_Me;
			newPlayer->sockfd		= INVALID_SOCKET;		// client has no connection to itself
			snprintf(newPlayer->name, sizeof(newPlayer->name), "YOU");		// TODO: get actual name

			break;
		}

		case kNSpPlayerJoined:
		{
			NSpPlayerJoinedMessage* joinedMessage = (NSpPlayerJoinedMessage*) message;

			if (message->messageLen < sizeof(NSpPlayerJoinedMessage))	// a short message would over-read playerInfo / name
				break;

			NSpPlayer* newPlayer = NSpGame_GetPlayerFromID(game, joinedMessage->playerInfo.id);
			if (!newPlayer)
			{
				printf("kNSpPlayerJoined: invalid id %d; ignoring.\n", (int) joinedMessage->playerInfo.id);
				break;
			}

			newPlayer->id			= joinedMessage->playerInfo.id;
			newPlayer->state		= kNSpPlayerState_ConnectedPeer;
			newPlayer->sockfd		= INVALID_SOCKET;		// client has no p2p connection to other players
			CopyPlayerName(newPlayer->name, joinedMessage->playerInfo.name);

			break;
		}

		case kNSpPlayerLeft:
		{
			NSpPlayerLeftMessage* leftMessage = (NSpPlayerLeftMessage*) message;

			if (message->messageLen < sizeof(NSpPlayerLeftMessage))
				break;

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

	game->hostListenSocket		= CreateTCPSocket(true);

	if (!IsSocketValid(game->hostListenSocket))
	{
		goto fail;
	}

	int listenRC = listen(game->hostListenSocket, PENDING_CONNECTIONS_QUEUE);

	if (listenRC != 0)
	{
		goto fail;
	}

	// Create a player for myself
	NSpPlayer* me = NSpGame_GetPlayerFromID(game, kNSpHostID);
	if (me == NULL)
	{
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
		// Validate the advertisement payload: only register a host that actually sent our lobby
		// magic string (must match the advertiser in NSpGame_AdvertiseTick). Without this, any
		// stray UDP datagram on the port registered a bogus "game" the client would try to join.
		static const char kLobbyMagic[] = "JOIN MY CMR GAME";
		if (receivedBytes < (ssize_t) (sizeof(kLobbyMagic) - 1) ||
			memcmp(message, kLobbyMagic, sizeof(kLobbyMagic) - 1) != 0)
		{
			return kNSpRC_OK;		// not one of our advertisements — ignore it
		}

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

	// iOS 14.4+ blocks INADDR_BROADCAST without multicast entitlement.
	// Use subnet-directed broadcast instead on iOS.
#if __IOS__ || __TVOS__
	struct in_addr subnetBroadcast = GetSubnetBroadcastAddress();
#endif

	struct sockaddr_in broadcastAddr =
	{
		.sin_family = AF_INET,
		.sin_port = htons(gNetPort),
#if __IOS__ || __TVOS__
		.sin_addr = subnetBroadcast,
#else
		.sin_addr.s_addr = INADDR_BROADCAST,
#endif
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

#pragma mark - Send rings

static void SendRing_Reset(SendRing* r)
{
	r->head = 0;
	r->tail = 0;
	r->used = 0;
	// highWater is per-session telemetry; it is cleared by the struct memset on alloc/clear.
}

static uint32_t SendRing_FreeSpace(const SendRing* r)
{
	return SEND_RING_CAPACITY - r->used;
}

// Append 'len' bytes to the ring. Returns false on overflow (caller treats as a send failure).
static bool SendRing_Append(SendRing* r, const uint8_t* src, uint32_t len)
{
	if (len > SendRing_FreeSpace(r))
	{
		return false;
	}

	uint32_t first = SEND_RING_CAPACITY - r->tail;		// contiguous run to the buffer end
	if (first > len)
	{
		first = len;
	}

	memcpy(r->data + r->tail, src, first);
	if (len > first)
	{
		memcpy(r->data, src + first, len - first);		// wrap-around to the start
	}

	r->tail = (r->tail + len) % SEND_RING_CAPACITY;
	r->used += len;
	if (r->used > r->highWater)
	{
		r->highWater = r->used;
	}

	return true;
}

// Non-blocking drain of contiguous runs until the ring is empty or the socket would block.
// Returns kNSpRC_OK (drained or buffer full) or kNSpRC_SendFailed (peer closed / hard error).
static int SendRing_Drain(SendRing* r, sockfd_t sockfd)
{
	if (!IsSocketValid(sockfd))
	{
		return kNSpRC_SendFailed;
	}

	while (r->used > 0)
	{
		uint32_t run = SEND_RING_CAPACITY - r->head;	// contiguous bytes from head
		if (run > r->used)
		{
			run = r->used;
		}

		ssize_t sent = send(sockfd, (const char*)(r->data + r->head), run, MSG_NOSIGNAL);

		if (sent > 0)
		{
			r->head = (r->head + (uint32_t)sent) % SEND_RING_CAPACITY;
			r->used -= (uint32_t)sent;
			continue;
		}

		if (sent == 0)
		{
			return kNSpRC_SendFailed;					// peer closed
		}

		if (GetSocketError() == kSocketError_WouldBlock)
		{
			return kNSpRC_OK;							// buffer full; retry next pump
		}

		return kNSpRC_SendFailed;						// EPIPE/ECONNRESET/etc. -> dead
	}

	return kNSpRC_OK;
}

#pragma mark - NSpMessage

// Blocking send retained ONLY for the one-shot, non-gameplay lobby/teardown sends (join
// request, game-full denial) where no per-socket ring exists yet. NEVER use on the
// per-frame gameplay path — that is what SendOrEnqueue + the send rings are for.
static int SendBytesBlocking(sockfd_t sockfd, NSpMessageHeader* header)
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

	const char* dataPtr = (const char*)header;
	size_t remaining = header->messageLen;
	size_t totalSent = 0;

	// Retry loop for EWOULDBLOCK - important for slow WiFi clients where
	// the send buffer may fill up temporarily.
	// We use a high retry count because we MUST send the whole message or connection state will de-sync.
	const int maxRetries = 100;
	const int retryDelayMs = 2; // Short delay to avoid hanging the game loop too long
	int retries = 0;

	while (remaining > 0)
	{
		ssize_t sent = send(
			sockfd,
			dataPtr + totalSent,
			remaining,
			MSG_NOSIGNAL
		);

		if (sent > 0)
		{
			totalSent += sent;
			remaining -= sent;
			retries = 0; // Reset retries on progress
			
			// Optional: log progress if it was a partial send
			// if (remaining > 0) printf("Partial send: %zd/%u\n", totalSent, header->messageLen);
		}
		else if (sent == 0)
		{
			// Connection closed by peer?
			printf("%s: connection closed on socket %d\n", __func__, (int)sockfd);
			return kNSpRC_SendFailed;
		}
		else // sent < 0
		{
			int err = GetSocketError();
			if (err == kSocketError_WouldBlock)
			{
				retries++;
				if (retries >= maxRetries)
				{
					printf("%s: timeout sending message on socket %d\n", __func__, (int)sockfd);
					return kNSpRC_SendFailed;
				}
				SDL_Delay(retryDelayMs);
			}
			else
			{
				printf("%s: error sending message on socket %d: %d\n", __func__, (int)sockfd, err);
				return kNSpRC_SendFailed;
			}
		}
	}

	return kNSpRC_OK;
}

// Non-blocking enqueue: one send() attempt when the ring is empty, append the remainder
// (or the whole message) to the ring otherwise. Returns kNSpRC_SendFailed on overflow
// (~2s backlog) or a hard socket error -> the caller routes it to the existing kick (host)
// / terminate (client) path. The main thread never blocks here.
static int SendOrEnqueue(sockfd_t sockfd, SendRing* ring, NSpMessageHeader* header)
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

	uint32_t msgLen = header->messageLen;

	// TCP is a byte stream: if anything is already queued we MUST append (sending fresh
	// bytes now would interleave them ahead of the backlog and corrupt the stream).
	if (ring->used == 0)
	{
		ssize_t sent = send(sockfd, (const char*)header, msgLen, MSG_NOSIGNAL);

		if (sent == (ssize_t)msgLen)
		{
			return kNSpRC_OK;								// healthy path: whole message out immediately
		}

		if (sent > 0)
		{
			// Partial send on an empty ring: queue exactly the unsent remainder.
			return SendRing_Append(ring, (const uint8_t*)header + sent, msgLen - (uint32_t)sent)
				? kNSpRC_OK : kNSpRC_SendFailed;
		}

		if (sent == 0)
		{
			return kNSpRC_SendFailed;						// peer closed
		}

		if (GetSocketError() != kSocketError_WouldBlock)
		{
			return kNSpRC_SendFailed;						// hard error
		}

		// EWOULDBLOCK: fall through and queue the whole message.
	}

	return SendRing_Append(ring, (const uint8_t*)header, msgLen)
		? kNSpRC_OK : kNSpRC_SendFailed;					// false == overflow -> caller's kick / terminate path
}

// Drains each socket's send ring with non-blocking send()s. Called every Net_Pump (frame
// start + inside the Stage-1 blocking receive loops). Host: a drain failure kicks that one
// client (the kick's PlayerLeft broadcast only appends into OTHER slots' rings). Client:
// a failure closes the uplink so the existing PollSocket broken-pipe path synthesizes
// kNSpGameTerminated(NetworkError) — no NetLow->NetHigh fatal-call dependency.
void NSpGame_FlushSends(NSpGameReference gameRef)
{
	NSpGame* game = NSpGame_Unbox(gameRef);

	if (!game)
	{
		return;
	}

	if (game->isHosting)
	{
		for (int i = 0; i < MAX_CLIENTS; i++)
		{
			NSpPlayer* peer = &game->players[i];

			if (!IsSocketValid(peer->sockfd))
			{
				SendRing_Reset(&peer->sendRing);
				continue;
			}

			if (SendRing_Drain(&peer->sendRing, peer->sockfd) != kNSpRC_OK)
			{
				// Do NOT NSpPlayer_Kick directly here: that would clear the slot + tell the OTHER
				// clients, but never run PlayerUnexpectedlyLeavesGame on the host itself (this is a
				// send path, not the message-get path). Host and clients would then disagree on
				// isComputer -> divergent synced-RNG draw counts -> randomSeed desync FATAL. Defer:
				// the next NSpMessage_GetAsHost hands the host's own NetHigh the kNSpPlayerLeft.
				NSpPlayer_DeferLeaveNotify(peer);
			}
		}
	}
	else
	{
		if (IsSocketValid(game->clientToHostSocket))
		{
			if (SendRing_Drain(&game->clientSendRing, game->clientToHostSocket) != kNSpRC_OK)
			{
				CloseSocket(&game->clientToHostSocket);		// next NSpMessage_Get -> kNSpGameTerminated
				SendRing_Reset(&game->clientSendRing);
			}
		}
	}
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
				for (int i = 0; i < MAX_CLIENTS; i++)
				{
					NSpPlayer* peer = &game->players[i];

					if (peer->state == kNSpPlayerState_ConnectedPeer	// send to peers with complete handshake
						&& peer->id != header->from)					// don't return to sender!
					{
						int rc = SendOrEnqueue(peer->sockfd, &peer->sendRing, header);
						if (rc != kNSpRC_OK && kickOnFail)
						{
							// A single slow/dead peer is RESOLVED by dropping that one peer (the
							// deferred leave path notifies the host's own NetHigh + the other
							// clients). It must NOT bubble up as the broadcast's return value:
							// HostSend_ControlInfoToClients fatals the WHOLE session on a non-zero
							// return, so one overflowing client would otherwise nuke everyone.
							NSpPlayer_DeferLeaveNotify(peer);
						}
					}
				}
				// Per-peer failures are handled by the kick above; the broadcast as a whole succeeds.
				return kNSpRC_OK;
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
					int rc = SendOrEnqueue(peer->sockfd, &peer->sendRing, header);
					if (rc != kNSpRC_OK && kickOnFail)
					{
						// Defer the kick so the host's own NetHigh is notified of the departure at
						// the same logical point the remote clients are (see NSpMessage_GetAsHost).
						NSpPlayer_DeferLeaveNotify(peer);
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

		int rc = SendOrEnqueue(game->clientToHostSocket, &game->clientSendRing, header);
		if (rc != kNSpRC_OK)
		{
			// Ring overflow or hard socket error: tear down the uplink so the next
			// NSpMessage_Get synthesizes kNSpGameTerminated(NetworkError) through the
			// existing broken-pipe path (no NetLow->NetHigh fatal-call dependency).
			CloseSocket(&game->clientToHostSocket);
			SendRing_Reset(&game->clientSendRing);
		}

		return rc;
	}
}

#pragma mark - NSpCMRClient

// Host-side: a send to this peer just failed (ring overflow ~2s backlog, or a hard socket
// error). We are on a send path, NOT the message-get path, so we cannot hand the host's own
// NetHigh a kNSpPlayerLeft from here. Defer it: drop the socket and flag the slot. The next
// NSpMessage_GetAsHost synthesizes the local kNSpPlayerLeft (running PlayerUnexpectedlyLeavesGame
// in lockstep with the remote clients) and broadcasts the leave to the other clients via
// NSpPlayer_Kick. This keeps host vs client agreement on isComputer / gNumGatheredPlayers and the
// synced-RNG draw count, which a direct NSpPlayer_Kick from a send path would break.
static void NSpPlayer_DeferLeaveNotify(NSpPlayer* peer)
{
	peer->needsLeaveNotify = true;
	CloseSocket(&peer->sockfd);		// no further send()/recv() on this dead peer; flag drives cleanup
}

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

int NSpPlayer_DisconnectForProtocolViolation(NSpGameReference gameRef, NSpPlayerID playerID)
{
	NSpGame* game = NSpGame_Unbox(gameRef);
	if (!game || !game->isHosting)
		return kNSpRC_NoGame;

	NSpPlayer* player = NSpGame_GetPlayerFromID(gameRef, playerID);
	if (!player || player->state == kNSpPlayerState_Offline || player->state == kNSpPlayerState_Me)
		return kNSpRC_InvalidPlayer;

	printf("%s: disconnecting player %d\n", __func__, (int) playerID);
	NSpPlayer_DeferLeaveNotify(player);
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

uint32_t NSpPlayer_GetLastHeard(NSpGameReference gameRef, NSpPlayerID playerID)
{
	NSpPlayer* player = NSpGame_GetPlayerFromID(gameRef, playerID);
	return player ? player->lastHeard : 0;
}

uint32_t NSpGame_GetHostLastHeard(NSpGameReference gameRef)
{
	NSpGame* game = NSpGame_Unbox(gameRef);
	return game ? game->hostLastHeard : 0;
}

// CMR7 Stage 4: refresh every liveness clock to now. Called at game-loop entry so the long
// (possibly >NET_DROP_MS) lobby/vehicle-select/level-load gap can never trip a false drop on the
// first in-game NetCheck_ConnectionTimeouts.
void NSpGame_TouchAllLastHeard(NSpGameReference gameRef)
{
	NSpGame* game = NSpGame_Unbox(gameRef);
	if (!game)
		return;

	uint32_t now = (uint32_t) SDL_GetTicks();
	game->hostLastHeard = now;
	for (int i = 0; i < MAX_CLIENTS; i++)
		game->players[i].lastHeard = now;
}
