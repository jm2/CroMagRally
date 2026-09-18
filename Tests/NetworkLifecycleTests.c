// Compile the real transport in this translation unit to inject only the clock and
// inspect private ring/slot state. All traffic uses actual loopback TCP/UDP sockets.
#include <SDL3/SDL.h>
#include <poll.h>
#include <stdarg.h>
#include <stdlib.h>
static uint32_t testNow = 100;
static bool testClockRuns;
static Uint64 testClockStartedAt;
static Uint64 TestTicks(void)
{
    return testNow + (testClockRuns ? SDL_GetTicks() - testClockStartedAt : 0);
}
#define SDL_GetTicks TestTicks
#include "../Source/Network/NetLow.c"
#undef SDL_GetTicks

#define CHECK(c) do { if (!(c)) DoFatalAlert("line %d: %s", __LINE__, #c); } while (0)
void* AllocPtr(long size) { void* p = malloc((size_t)size); CHECK(p); return p; }
void* AllocPtrClear(long size) { void* p = calloc(1, (size_t)size); CHECK(p); return p; }
void SafeDisposePtr(void* p) { free(p); }
void DoSDLMaintenance(void) {}
void DoFatalAlert(const char* format, ...)
{
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fputc('\n', stderr);
    exit(1);
}

static struct sockaddr_in Address(sockfd_t socket)
{
    struct sockaddr_in address;
    socklen_t size = sizeof(address);
    CHECK(getsockname(socket, (struct sockaddr*)&address, &size) == 0);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    return address;
}

static void WaitReadable(sockfd_t socket)
{
    // The peer's connect/send can finish before our nonblocking socket is ready.
    // Wait on real socket readiness without advancing the injected protocol clock.
    struct pollfd readiness = {.fd = socket, .events = POLLIN};
    int result = poll(&readiness, 1, 1000);
    if (result != 1 || !(readiness.revents & POLLIN))
        DoFatalAlert("Loopback socket %d not readable (poll=%d, events=%d, errno=%d)",
            socket, result, readiness.revents, errno);
}

static NSpPlayerID AcceptClient(NSpGame* host)
{
    WaitReadable(host->hostListenSocket);
    return NSpGame_AcceptNewClient(host);
}

static NSpMessageHeader* WaitMessage(NSpGame* game)
{
    for (int i = 0; i < 1000; i++)
    {
        NSpMessageHeader* message = NSpMessage_Get(game);
        if (message) return message;
        SDL_Delay(1);
    }
    DoFatalAlert("Timed out waiting for loopback message");
}

static void Drain(NSpGame* game)
{
    NSpMessageHeader* message;
    while ((message = NSpMessage_Get(game)))
        NSpMessage_Release(game, message);
}

static NSpGame* Join(NSpGame* host)
{
    LobbyInfo lobby = {.hostAddr = Address(host->hostListenSocket)};
    NSpGame* client = JoinLobby(&lobby);
    CHECK(client);
    CHECK(AcceptClient(host) > 0);
    NSpMessageHeader* request = WaitMessage(host);
    CHECK(request->what == kNSpJoinRequest);
    CHECK(NSpGame_AckJoinRequest(host, request) == kNSpRC_OK);
    NSpMessage_Release(host, request);
    NSpMessageHeader* approved = WaitMessage(client);
    CHECK(approved->what == kNSpJoinApproved);
    NSpMessage_Release(client, approved);
    Drain(client);
    CHECK(client->myID > 0);
    return client;
}

static int ExpectLeave(NSpGame* host)
{
    NSpMessageHeader* message = WaitMessage(host);
    CHECK(message->what == kNSpPlayerLeft);
    int id = ((NSpPlayerLeftMessage*)message)->playerID;
    NSpMessage_Release(host, message);
    return id;
}

static void Session(void)
{
    NSpGame* host = NSpGame_Host();
    CHECK(host);
    struct sockaddr_in address = Address(host->hostListenSocket);
    gNetPort = ntohs(address.sin_port);
    CHECK(NSpGame_GetActivePlayersIDMask(host) == 1);

    // CMR7 readiness fields were uninitialized: refuse that peer at the handshake
    // instead of accepting it and then disconnecting during level preparation.
    int legacy = socket(AF_INET, SOCK_STREAM, 0);
    CHECK(connect(legacy, (struct sockaddr*)&address, sizeof(address)) == 0);
    CHECK(AcceptClient(host) == 1);
    NSpJoinRequestMessage legacyJoin = {0};
    NSpClearMessageHeader(&legacyJoin.header);
    legacyJoin.header.version = 'CMR7';
    legacyJoin.header.what = kNSpJoinRequest;
    legacyJoin.header.to = kNSpHostID;
    legacyJoin.header.messageLen = sizeof(legacyJoin);
    CHECK(send(legacy, &legacyJoin, sizeof(legacyJoin), MSG_NOSIGNAL) == sizeof(legacyJoin));
    for (int i = 0; i < 1000 && host->players[1].state != kNSpPlayerState_Offline; i++)
    {
        CHECK(!NSpMessage_Get(host));
        SDL_Delay(1);
    }
    CHECK(host->players[1].state == kNSpPlayerState_Offline);
    CHECK(NSpGame_GetActivePlayersIDMask(host) == 1);
    CloseSocket(&legacy);

    // A send failure before join approval also recycles silently.
    LobbyInfo pendingLobby = {.hostAddr = address};
    NSpGame* pending = JoinLobby(&pendingLobby);
    CHECK(pending && AcceptClient(host) == 1);
    NSpMessageHeader* pendingRequest = WaitMessage(host);
    CHECK(pendingRequest->what == kNSpJoinRequest);
    uint8_t handshakeBacklog[SEND_RING_CAPACITY] = {0};
    CHECK(SendRing_Append(&host->players[1].sendRing, handshakeBacklog, sizeof(handshakeBacklog)));
    CHECK(NSpGame_AckJoinRequest(host, pendingRequest) != kNSpRC_OK);
    NSpMessage_Release(host, pendingRequest);
    CHECK(host->players[1].state == kNSpPlayerState_Offline);
    CHECK(!NSpMessage_Get(host));
    NSpGame_Dispose(pending, 0);

    // Fill every slot without completing a handshake, including a partial header.
    int silent[MAX_CLIENTS - 1];
    for (int i = 0; i < MAX_CLIENTS - 1; i++)
    {
        silent[i] = socket(AF_INET, SOCK_STREAM, 0);
        CHECK(connect(silent[i], (struct sockaddr*)&address, sizeof(address)) == 0);
        CHECK(AcceptClient(host) == i + 1);
    }
    CHECK(send(silent[0], "C", 1, MSG_NOSIGNAL) == 1);
    testNow += NSP_HANDSHAKE_TIMEOUT_MS - 1;
    CHECK(!NSpMessage_Get(host));
    CHECK(host->players[1].state == kNSpPlayerState_AwaitingHandshake);
    host->players[1].lastHeard = testNow; // activity cannot extend the accept deadline
    testNow++;
    CHECK(!NSpMessage_Get(host));
    for (int i = 1; i < MAX_CLIENTS; i++)
        CHECK(host->players[i].state == kNSpPlayerState_Offline);
    for (int i = 0; i < MAX_CLIENTS - 1; i++) CloseSocket(&silent[i]);

    NSpGame* first = Join(host);
    NSpGame* second = Join(host);
    CHECK(first->myID == 1 && second->myID == 2);
    Drain(first);
    NSpGame_Dispose(first, 0);
    CHECK(ExpectLeave(host) == 1);
    CHECK(NSpGame_GetActivePlayersIDMask(host) == 5); // host + sparse ID 2
    Drain(second);
    NSpGame* replacement = Join(host);
    CHECK(replacement->myID == 1);
    CHECK(host->players[1].sendRing.used == 0 && !host->players[1].needsLeaveNotify);
    Drain(second);

    // Force the actual send path to overflow one ring. The surviving peer and host
    // must each receive one leave while the broadcast as a whole still succeeds.
    uint8_t backlog[SEND_RING_CAPACITY] = {0};
    CHECK(SendRing_Append(&host->players[1].sendRing, backlog, sizeof(backlog)));
    NSpMessageHeader heartbeat;
    NSpClearMessageHeader(&heartbeat);
    heartbeat.what = kNetKeepAliveMessage;
    heartbeat.messageLen = sizeof(heartbeat);
    heartbeat.from = kNSpHostID;
    heartbeat.to = kNSpAllPlayers;
    CHECK(NSpMessage_Send(host, &heartbeat, kNSpSendFlag_Registered) == kNSpRC_OK);
    CHECK(host->players[1].needsLeaveNotify);
    CHECK(ExpectLeave(host) == 1);
    CHECK(!NSpMessage_Get(host));
    NSpMessageHeader* message = WaitMessage(second);
    CHECK(message->what == kNetKeepAliveMessage);
    NSpMessage_Release(second, message);
    CHECK(ExpectLeave(second) == 1);
    CHECK(NSpGame_GetActivePlayersIDMask(second) == 5);
    NSpGame_Dispose(replacement, 0);

    replacement = Join(host);
    Drain(second);
    NSpGame_Dispose(second, 0);
    NSpGame_Dispose(replacement, 0);
    int a = ExpectLeave(host), b = ExpectLeave(host);
    CHECK(a != b && (a == 1 || a == 2) && (b == 1 || b == 2));
    CHECK(!NSpMessage_Get(host));
    CHECK(NSpGame_GetActivePlayersIDMask(host) == 1);
    CHECK(NSpGame_Dispose(host, 0) == kNSpRC_OK);
}

static sockfd_t BoundLoopbackSocket(int type)
{
    sockfd_t sock = socket(AF_INET, type, 0);
    CHECK(IsSocketValid(sock));
    struct sockaddr_in address = {.sin_family = AF_INET, .sin_addr.s_addr = htonl(INADDR_LOOPBACK)};
    CHECK(bind(sock, (struct sockaddr*)&address, sizeof(address)) == 0);
    CHECK(MakeSocketNonBlocking(sock));
    return sock;
}

static void Discovery(void)
{
    NSpSearch* search = AllocPtrClear(sizeof(*search));
    search->listenSocket = BoundLoopbackSocket(SOCK_DGRAM);
    sockfd_t advertiser = BoundLoopbackSocket(SOCK_DGRAM);
    struct sockaddr_in address = Address(search->listenSocket);
    testNow = UINT32_MAX - 2000;
    const char advertisement[] = "JOIN MY CMR GAME";
    CHECK(sendto(advertiser, advertisement, sizeof(advertisement), 0, (struct sockaddr*)&address, sizeof(address)) > 0);
    WaitReadable(search->listenSocket);
    CHECK(NSpSearch_Tick(search) == 0 && NSpSearch_GetNumGamesFound(search) == 1);
    testNow += 4000; // crosses the 32-bit clock wrap
    CHECK(sendto(advertiser, advertisement, sizeof(advertisement), 0, (struct sockaddr*)&address, sizeof(address)) > 0);
    WaitReadable(search->listenSocket);
    CHECK(NSpSearch_Tick(search) == 0 && NSpSearch_GetNumGamesFound(search) == 1);
    testNow += NSP_LOBBY_EXPIRY_MS - 1;
    CHECK(NSpSearch_GetNumGamesFound(search) == 1);
    testNow++;
    CHECK(NSpSearch_GetNumGamesFound(search) == 0);
    CloseSocket(&advertiser);

    sockfd_t listener = BoundLoopbackSocket(SOCK_STREAM);
    CHECK(listen(listener, 2) == 0);
    struct sockaddr_in live = Address(listener);
    gNetPort = ntohs(live.sin_port);
    search->gamesFound[0] = (LobbyInfo){.hostAddr = live, .lastAdvertised = testNow};
    search->gamesFound[0].hostAddr.sin_addr.s_addr = htonl(0x7f000002); // no listener on this address
    search->gamesFound[1] = (LobbyInfo){.hostAddr = live, .lastAdvertised = testNow};
    search->numGamesFound = 2;
    // Some hosts time out this unbound loopback address instead of refusing it.
    // Let the real connection deadline advance while retaining the injected epoch.
    testClockStartedAt = SDL_GetTicks();
    testClockRuns = true;
    CHECK(!NSpSearch_JoinGame(search, 0));
    testNow = (uint32_t)TestTicks();
    testClockRuns = false;
    CHECK(NSpSearch_GetNumGamesFound(search) == 1);
    NSpGame* client = NSpSearch_JoinGame(search, 0);
    CHECK(client);
    WaitReadable(listener);
    sockfd_t accepted = accept(listener, NULL, NULL);
    CHECK(IsSocketValid(accepted));
    CloseSocket(&accepted);
    CloseSocket(&listener);
    NSpGame_Dispose(client, 0);
    NSpSearch_Dispose(search);
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0); // retain transport diagnostics if CTest times out
    gNetPort = 0;
    Session();
    Session(); // immediately rehost on the same port in the same process
    Discovery();
    puts("Loopback lifecycle tests passed");
    return 0;
}
