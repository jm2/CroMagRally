// Reuse the real loopback transport fixture, then exercise production NetHigh
// readiness/event helpers plus production battle damage/victory logic. Linker
// section GC omits unrelated graphics/UI paths; wrappers isolate tagged RNG/UI.
#define main TransportTestMain
#include "NetworkLifecycleTests.c"
#undef main
static Boolean TestGatherScreen(void);
#define DoNetGatherScreen TestGatherScreen
#define SDL_GetTicks TestTicks
#include "../Source/Network/NetHigh.c"
#undef SDL_GetTicks
#undef DoNetGatherScreen
#include "../Source/Screens/NetGather.c"

Boolean gSimulationPaused;
float gFramesPerSecondFrac, gStartingLightTimer;
long gNumCheckpoints = 1;
short gWorstHumanPlace;
static ObjNode playerModels[MAX_PLAYERS];
static int taggedChoices;
static Boolean backPressed;
static int expectedGatherState, gatherScreenCalls;
static Byte winLoseMode[MAX_PLAYERS];
static short winLoseWinner[MAX_PLAYERS];
static Boolean TestGatherScreen(void)
{
    CHECK(gNetSequenceState == expectedGatherState);
    gatherScreenCalls++;
    return true;
}
void __wrap_ChooseTaggedPlayer(void)
{
    taggedChoices++;
    ChooseTaggedPlayerWithIndex(0);
}
void __wrap_UpdateTagMarker(void) {}
void ShowWinLose(short playerNum, Byte mode, short winner)
{
    CHECK(playerNum >= 0 && playerNum < gNumTotalPlayers);
    winLoseMode[playerNum] = mode;
    winLoseWinner[playerNum] = winner;
}
Boolean GetNewNeedStateAnyP(int needID) { return needID == kNeed_UIBack && backPressed; }

// Keep real level player initialization; isolate terrain/model/physics setup.
int GetNumAgesCompleted(void) { return 0; }
uint16_t RandomRange(unsigned short min, unsigned short max) { CHECK(min <= max); return min; }
float GetTerrainY(float x, float z) { (void)x; (void)z; return 0; }
ObjNode* InitPlayer_Car(int playerNum, OGLPoint3D* where, float rotY)
{
    (void)where; (void)rotY;
    return gPlayerInfo[playerNum].objNode = &playerModels[playerNum];
}
ObjNode* InitPlayer_Submarine(int playerNum, OGLPoint3D* where, float rotY)
{
    return InitPlayer_Car(playerNum, where, rotY);
}
void SetPhysicsForVehicleType(short playerNum) { (void)playerNum; }
void SetDefaultCameraModeForAllPlayers(void) {}

static NSpGame* BeginSession(NSpGame** first, NSpGame** second)
{
    ResetNetGameTransientState();
    memset(gPlayerInfo, 0, sizeof(gPlayerInfo));
    gNetGameInProgress = true;
    gIsNetworkHost = true;
    gIsNetworkClient = false;
    gGameOver = false;
    gTrackCompleted = false;
    gNumPlayersEliminated = 0;
    gFramesPerSecondFrac = 1.0f / 60.0f;
    gStartingLightTimer = 0;
    memset(winLoseMode, 0, sizeof(winLoseMode));
    memset(winLoseWinner, 0, sizeof(winLoseWinner));
    backPressed = false;
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
    for (int i = 0; i < gNumTotalPlayers; i++) gPlayerInfo[i].health = 1;
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
    CHECK(gNumPlayersEliminated == 0); // race bots do not affect battle bookkeeping
    CHECK(ExpectLeave(second) == 1);
    EndSession(host, first, second);
}

static void LastPeerTimeout(void)
{
    NSpGame *first, *second;
    NSpGame* host = BeginSession(&first, &second);
    NSpPlayer_Kick(host, 2);
    ApplyBecomeBot(1);
    CHECK(gNumGatheredPlayers == 2);
    ClearPlayerSyncMask();
    MarkPlayerSynced(0);
    gNetSequenceState = kNetSequence_WaitingForPlayerVehicles;
    testNow += VEHICLE_READY_TIMEOUT_MS;
    HostAdvanceReadinessBarrier(VEHICLE_READY_TIMEOUT_MS,
        kNetSequence_WaitingForPlayerVehicles, kNetSequence_GotAllPlayerVehicles);
    CHECK(gNetSequenceState == kNetSequence_OfflineEverybodyLeft && gGameOver);
    CHECK(DoNetGatherControls() == 0);
    CHECK(!gNetGameInProgress && !gNetGame && !gIsNetworkHost);
    CHECK(gNetSequenceState == kNetSequence_OfflineEverybodyLeft);
    CHECK(DoNetGatherControls() == 0); // Display the error until it is acknowledged.
    backPressed = true;
    CHECK(DoNetGatherControls() == -1);
    backPressed = false;
    NSpGame_Dispose(first, 0);
    NSpGame_Dispose(second, 0);
}

static void SelectorResumesAfterTeardown(void)
{
    NSpGame *first, *second;
    NSpGame* host = BeginSession(&first, &second);
    CHECK(host);
    EndNetworkGame(); // production teardown performed by a failed vehicle broadcast
    gNetSequenceState = kNetSequence_ClientOfflineBecauseKicked;
    expectedGatherState = gNetSequenceState;
    gatherScreenCalls = 0;
    CHECK(GetVehicleSelectionFromNetPlayers());
    CHECK(gatherScreenCalls == 1 && gNetSequenceState == expectedGatherState);
    CHECK(!gNetGame && !gNetGameInProgress && gPlayerSyncMask == 0);
    gNetSequenceState = kNetSequence_Offline;
    gatherScreenCalls = 0;
    CHECK(GetVehicleSelectionFromNetPlayers());
    CHECK(gatherScreenCalls == 0 && gNetSequenceState == kNetSequence_Offline);
    NSpGame_Dispose(first, 0);
    NSpGame_Dispose(second, 0);
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

static void ExpectSurvivalWinner(void)
{
    CHECK(gPlayerInfo[2].isEliminated && gNumPlayersEliminated == 1);
    PlayerLoseHealth(2, 1); // the dropped player cannot be eliminated twice
    ApplyBecomeBot(2); // nor can a duplicate disconnect count twice
    CHECK(gNumPlayersEliminated == 1 && gNumGatheredPlayers == 2);
    PlayerLoseHealth(1, 1);
    CHECK(gNumPlayersEliminated == 2 && !gPlayerInfo[0].isEliminated);
    UpdateGameModeSpecifics();
    CHECK(gTrackCompleted && !gPlayerInfo[0].isEliminated);
    for (int i = 0; i < gNumTotalPlayers; i++)
        CHECK(winLoseWinner[i] == 0 && winLoseMode[i] == (i == 0 ? 1 : 2));
}

static void SurvivalReadinessRemoval(bool alreadyEliminated)
{
    NSpGame *first, *second;
    NSpGame* host = BeginSession(&first, &second);
    gGameMode = GAME_MODE_SURVIVAL;
    if (alreadyEliminated) PlayerLoseHealth(2, 1);
    PlayerInfoType before[MAX_PLAYERS];
    memcpy(before, gPlayerInfo, sizeof(before));
    ClearPlayerSyncMask();
    MarkPlayerSynced(0);
    MarkPlayerSynced(2);
    gNetSequenceState = kNetSequence_HostWaitForPlayersToPrepareLevel;
    testNow += LEVEL_READY_TIMEOUT_MS;
    HostAdvanceReadinessBarrier(LEVEL_READY_TIMEOUT_MS,
        kNetSequence_HostWaitForPlayersToPrepareLevel, kNetSequence_GameLoop);
    CHECK(gNetSequenceState == kNetSequence_GameLoop && !gGameOver);
    ExpectSurvivalWinner();

    // Replay the actual leave notification against the surviving peer's pre-drop
    // state, then use real damage/victory logic to prove it reaches the same winner.
    NSpMessageHeader* leave = WaitMessage(second);
    CHECK(leave->what == kNSpPlayerLeft);
    memcpy(gPlayerInfo, before, sizeof(before));
    gNumPlayersEliminated = alreadyEliminated ? 1 : 0;
    gNumGatheredPlayers = 3;
    gTrackCompleted = false;
    gIsNetworkHost = false;
    gIsNetworkClient = true;
    gNetGame = second;
    gNetSequenceState = kNetSequence_ClientWaitForSyncFromHost;
    CHECK(!HandleOtherNetMessage(leave));
    NSpMessage_Release(second, leave);
    ExpectSurvivalWinner();
    EndSession(host, first, second);
}

static void BattleDepartureWinner(int mode)
{
    NSpGame *first, *second;
    NSpGame* host = BeginSession(&first, &second);
    gGameMode = mode;
    if (mode == GAME_MODE_TAG1)
    {
        for (int i = 0; i < gNumTotalPlayers; i++) gPlayerInfo[i].tagTimer = 60;
        ChooseTaggedPlayerWithIndex(1);
        gPlayerInfo[1].tagTimer = 0;
        UpdateGameModeSpecifics();
    }
    else
        PlayerLoseHealth(1, 1);
    CHECK(gNumPlayersEliminated == 1 && !gTrackCompleted);
    ApplyBecomeBot(2);
    ApplyBecomeBot(2);
    CHECK(gNumPlayersEliminated == 2 && gNumGatheredPlayers == 2);
    UpdateGameModeSpecifics();
    CHECK(gTrackCompleted && !gPlayerInfo[0].isEliminated);
    for (int i = 0; i < gNumTotalPlayers; i++)
        CHECK(winLoseWinner[i] == 0 && winLoseMode[i] == (i == 0 ? 1 : 2));
    EndSession(host, first, second);
}

static void VerifyVehicleTimeoutAfterInit(int mode)
{
    InitPlayersAtStartOfLevel();
    bool eliminationMode = mode == GAME_MODE_TAG1 || mode == GAME_MODE_SURVIVAL;
    CHECK(gPlayerInfo[2].isComputer);
    CHECK(gPlayerInfo[2].isEliminated == (mode != GAME_MODE_MULTIPLAYERRACE));
    CHECK(!gPlayerInfo[0].isEliminated && !gPlayerInfo[1].isEliminated);
    CHECK(gNumPlayersEliminated == (eliminationMode ? 1 : 0));
    if (mode == GAME_MODE_TAG1 || mode == GAME_MODE_TAG2)
    {
        ChooseTaggedPlayerWithIndex(2);
        CHECK(gWhoIsIt != 2); // the departed peer cannot be tagged after level init
    }
    if (mode == GAME_MODE_TAG1)
    {
        ChooseTaggedPlayerWithIndex(1);
        gPlayerInfo[1].tagTimer = 0;
        UpdateGameModeSpecifics();
    }
    else if (mode == GAME_MODE_SURVIVAL)
    {
        PlayerLoseHealth(1, 1);
        UpdateGameModeSpecifics();
    }
    if (eliminationMode)
    {
        CHECK(gTrackCompleted && !gPlayerInfo[0].isEliminated);
        for (int i = 0; i < gNumTotalPlayers; i++)
            CHECK(winLoseWinner[i] == 0 && winLoseMode[i] == (i == 0 ? 1 : 2));
    }
}

static void BattleVehicleTimeout(int mode)
{
    NSpGame *first, *second;
    NSpGame* host = BeginSession(&first, &second);
    gGameMode = mode;
    PlayerInfoType before[MAX_PLAYERS];
    memcpy(before, gPlayerInfo, sizeof(before));
    ClearPlayerSyncMask();
    MarkPlayerSynced(0);
    MarkPlayerSynced(2);
    gNetSequenceState = kNetSequence_WaitingForPlayerVehicles;
    testNow += VEHICLE_READY_TIMEOUT_MS;
    HostAdvanceReadinessBarrier(VEHICLE_READY_TIMEOUT_MS,
        kNetSequence_WaitingForPlayerVehicles, kNetSequence_GotAllPlayerVehicles);
    CHECK(gNetSequenceState == kNetSequence_GotAllPlayerVehicles && !gGameOver);
    VerifyVehicleTimeoutAfterInit(mode);

    NSpMessageHeader* leave = WaitMessage(second);
    CHECK(leave->what == kNSpPlayerLeft);
    memcpy(gPlayerInfo, before, sizeof(before));
    gNumPlayersEliminated = 0;
    gNumGatheredPlayers = 3;
    gTrackCompleted = false;
    gIsNetworkHost = false;
    gIsNetworkClient = true;
    gNetGame = second;
    gNetSequenceState = kNetSequence_WaitingForPlayerVehicles;
    CHECK(!HandleOtherNetMessage(leave));
    NSpMessage_Release(second, leave);
    VerifyVehicleTimeoutAfterInit(mode);
    EndSession(host, first, second);
}

static void SurvivalWithoutSurvivors(void)
{
    NSpGame *first, *second;
    NSpGame* host = BeginSession(&first, &second);
    gGameMode = GAME_MODE_SURVIVAL;
    for (int i = 0; i < gNumTotalPlayers; i++) PlayerLoseHealth(i, 1);
    UpdateGameModeSpecifics();
    CHECK(gTrackCompleted && gNumPlayersEliminated == gNumTotalPlayers);
    for (int i = 0; i < gNumTotalPlayers; i++)
        CHECK(winLoseWinner[i] == -1 && winLoseMode[i] == 2);
    EndSession(host, first, second);
}

static void TagWinnerDeparture(void)
{
    NSpGame *first, *second;
    NSpGame* host = BeginSession(&first, &second);
    gGameMode = GAME_MODE_TAG1;
    for (int i = 0; i < gNumTotalPlayers; i++) gPlayerInfo[i].tagTimer = 60;
    ChooseTaggedPlayerWithIndex(0);
    gPlayerInfo[0].tagTimer = 0;
    UpdateGameModeSpecifics();
    ChooseTaggedPlayerWithIndex(2);
    ApplyBecomeBot(1);
    UpdateGameModeSpecifics();
    CHECK(gTrackCompleted && winLoseWinner[2] == 2 && gPlayerInfo[2].isIt);
    int choicesBefore = taggedChoices;
    ApplyBecomeBot(2); // the winner disconnects during the result cooldown
    CHECK(gNumPlayersEliminated == 3 && !gPlayerInfo[2].isIt);
    CHECK(taggedChoices == choicesBefore); // nobody remains to select
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
    taggedChoices = 0;
    gPlayerInfo[2].isIt = true;
    gHostSendCounter = frame; // ApplyPendingFrameEvents applies the last transmitted frame
    ApplyPendingFrameEvents();
    CHECK(!gPlayerInfo[2].isComputer);
    gHostSendCounter = frame + 1;
    ApplyPendingFrameEvents();
    ApplyPendingFrameEvents();
    CHECK(gPlayerInfo[2].isComputer && !IsNetGamePaused());
    CHECK(gNumGatheredPlayers == 2 && taggedChoices == 1 && gNumPlayersEliminated == 1);
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
    LastPeerTimeout();
    SelectorResumesAfterTeardown();
    SurvivalReadinessRemoval(false);
    SurvivalReadinessRemoval(true);
    BattleDepartureWinner(GAME_MODE_SURVIVAL);
    BattleDepartureWinner(GAME_MODE_TAG1);
    BattleVehicleTimeout(GAME_MODE_SURVIVAL);
    BattleVehicleTimeout(GAME_MODE_TAG1);
    BattleVehicleTimeout(GAME_MODE_TAG2);
    BattleVehicleTimeout(GAME_MODE_CAPTUREFLAG);
    BattleVehicleTimeout(GAME_MODE_MULTIPLAYERRACE);
    SurvivalWithoutSurvivors();
    TagWinnerDeparture();
    PausedLeaveAndReset();
    puts("Readiness and paused-leave tests passed");
    return 0;
}
