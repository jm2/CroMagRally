// Reuse the real loopback transport fixture, then exercise production NetHigh
// readiness/event helpers. Linker section GC omits unrelated graphics/UI paths.
#define main TransportTestMain
#include "NetworkLifecycleTests.c"
#undef main
#define SDL_GetTicks TestTicks
#include "../Source/Network/NetHigh.c"
#undef SDL_GetTicks

PlayerInfoType gPlayerInfo[MAX_PLAYERS];
short gNumTotalPlayers, gNumRealPlayers;
Boolean gGameOver;
int gGameMode;
static int taggedChoices;
void ChooseTaggedPlayer(void) { taggedChoices++; }

static NSpGame* BeginSession(NSpGame** first, NSpGame** second)
{
    ResetNetGameTransientState();
    memset(gPlayerInfo, 0, sizeof(gPlayerInfo));
    gNetGameInProgress = true;
    gIsNetworkHost = true;
    gIsNetworkClient = false;
    gGameOver = false;
    gNumGatheredPlayers = gNumRealPlayers = gNumTotalPlayers = 3;
    gGameMode = GAME_MODE_MULTIPLAYERRACE;
    gNetPort = 0;
    NSpGame* host = NSpGame_Host();
    CHECK(host);
    gNetPort = ntohs(Address(host->hostListenSocket).sin_port);
    *first = Join(host);
    *second = Join(host);
    Drain(*first);
    gNetGame = host;
    // Dense game indices deliberately differ from sparse NSp IDs.
    gPlayerInfo[0].net.nspPlayerID = 0;
    gPlayerInfo[1].net.nspPlayerID = 2;
    gPlayerInfo[2].net.nspPlayerID = 1;
    return host;
}

static void EndSession(NSpGame* host, NSpGame* first, NSpGame* second)
{
    NSpGame_Dispose(first, 0);
    NSpGame_Dispose(second, 0);
    NSpGame_Dispose(host, 0);
    gNetGame = NULL;
}

static void Readiness(uint32_t timeout, int waiting, int ready)
{
    NSpGame *first, *second;
    NSpGame* host = BeginSession(&first, &second);
    ClearPlayerSyncMask();
    MarkPlayerSynced(0);
    MarkPlayerSynced(2);
    gNetSequenceState = waiting;
    gNetBadge[2] = true;
    testNow += timeout - 1;
    HostAdvanceReadinessBarrier(timeout, waiting, ready);
    CHECK(gNetSequenceState == waiting && NSpGame_GetNumActivePlayers(host) == 3);
    testNow++;
    HostAdvanceReadinessBarrier(timeout, waiting, ready);
    CHECK(gNetSequenceState == ready && !gGameOver);
    CHECK(NSpGame_GetActivePlayersIDMask(host) == 5);
    CHECK(gPlayerSyncMask == 5 && AreAllPlayersSynced());
    CHECK(gPlayerInfo[2].isComputer && !gPlayerInfo[1].isComputer && !gNetBadge[2]);
    CHECK(gNumGatheredPlayers == 2);
    CHECK(ExpectLeave(second) == 1);
    EndSession(host, first, second);
}

static void DelayedReady(void)
{
    NSpGame *first, *second;
    NSpGame* host = BeginSession(&first, &second);
    ClearPlayerSyncMask();
    MarkPlayerSynced(0);
    MarkPlayerSynced(1);
    gNetSequenceState = kNetSequence_HostWaitForPlayersToPrepareLevel;
    testNow += LEVEL_READY_TIMEOUT_MS - 1;
    MarkPlayerSynced(2);
    HostAdvanceReadinessBarrier(LEVEL_READY_TIMEOUT_MS,
        kNetSequence_HostWaitForPlayersToPrepareLevel, kNetSequence_GameLoop);
    CHECK(gNetSequenceState == kNetSequence_GameLoop && NSpGame_GetNumActivePlayers(host) == 3);
    EndSession(host, first, second);
}

static void PausedLeaveAndReset(void)
{
    NSpGame *first, *second;
    NSpGame* host = BeginSession(&first, &second);
    gNetSequenceState = kNetSequence_GameLoop;
    gPlayerInfo[2].net.pauseState = 1;
    gNetBadge[2] = true;
    CHECK(IsNetGamePaused());
    NSpPlayerLeftMessage leave = {.playerID = 1};
    ScheduleBecomeBotFromLeave(&leave);
    ScheduleBecomeBotFromLeave(&leave); // duplicate delivery cannot double-convert
    CHECK(!gNetBadge[2]);
    CHECK(!gPlayerInfo[2].isComputer && IsNetGamePaused());
    uint32_t frame = sFrameEventTable[0].effectiveFrame;
    CHECK(sFrameEventTable[0].valid);
    gGameMode = GAME_MODE_TAG1;
    gPlayerInfo[2].isIt = true;
    gHostSendCounter = frame; // ApplyPendingFrameEvents applies the last transmitted frame
    ApplyPendingFrameEvents();
    CHECK(!gPlayerInfo[2].isComputer);
    gHostSendCounter = frame + 1;
    ApplyPendingFrameEvents();
    ApplyPendingFrameEvents();
    CHECK(gPlayerInfo[2].isComputer && !IsNetGamePaused());
    CHECK(gNumGatheredPlayers == 2 && taggedChoices == 1);
    gNetBadge[1] = true;
    gPlayerSyncMask = 7;
    ResetNetGameTransientState();
    CHECK(gPlayerSyncMask == 0 && gReadinessStartedMs == 0 && gHostSendCounter == 0);
    CHECK(!gNetBadge[1] && !sFrameEventTable[0].valid && !sHostPendingEvents[0].active);
    EndSession(host, first, second);
}

int main(void)
{
    Readiness(VEHICLE_READY_TIMEOUT_MS, kNetSequence_WaitingForPlayerVehicles, kNetSequence_GotAllPlayerVehicles);
    Readiness(LEVEL_READY_TIMEOUT_MS, kNetSequence_HostWaitForPlayersToPrepareLevel, kNetSequence_GameLoop);
    DelayedReady();
    PausedLeaveAndReset();
    puts("Readiness and paused-leave tests passed");
    return 0;
}
