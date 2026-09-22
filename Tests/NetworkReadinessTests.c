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
CommandLineOptions gCommandLine;
float gFramesPerSecondFrac, gStartingLightTimer;
long gNumCheckpoints = 1;
short gWorstHumanPlace;
static ObjNode playerModels[MAX_PLAYERS];
static int unlockedAges;
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
void DoAlert(const char* format, ...)
{
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fputc('\n', stderr);
    exit(1); // no scenario here expects a user-facing alert
}

// Keep real level player initialization; isolate terrain/model/physics setup.
int GetNumAgesCompleted(void) { return unlockedAges; }
static int randomRangeCalls;
uint16_t RandomRange(unsigned short min, unsigned short max) { CHECK(min <= max); randomRangeCalls++; return min; }
static OGLPoint2D terrainQueries[MAX_PLAYERS];
static int numTerrainQueries;
float GetTerrainY(float x, float z)
{
    if (numTerrainQueries < MAX_PLAYERS)
        terrainQueries[numTerrainQueries] = (OGLPoint2D){x, z};
    numTerrainQueries++;
    return 0;
}
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

#define SMALL_SESSION 3 // host + two clients: the original sparse-ID scenarios
#define FULL_SESSION MAX_CLIENTS // host + every client slot

// Dense game indices deliberately differ from sparse NSp IDs: clients take IDs in
// reverse player order (3 peers: player 1 <-> ID 2, player 2 <-> ID 1).
static NSpPlayerID PlayerNSpID(int numPlayers, int playerNum)
{
    return playerNum == 0 ? kNSpHostID : numPlayers - playerNum;
}

static int NSpIDPlayer(int numPlayers, NSpPlayerID id)
{
    return id == kNSpHostID ? 0 : numPlayers - id;
}

static uint32_t AllPeersMask(int numPlayers)
{
    return (uint32_t) ((1ull << numPlayers) - 1);
}

// Host plus numPlayers - 1 real loopback clients. peers[id] is the client whose NSp
// ID is id; peers[kNSpHostID] is the host.
static NSpGame* BeginSession(int numPlayers, NSpGame* peers[MAX_CLIENTS])
{
    CHECK(numPlayers >= 2 && numPlayers <= MAX_CLIENTS);
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
    gNumGatheredPlayers = gNumRealPlayers = gNumTotalPlayers = numPlayers;
    gGameMode = GAME_MODE_MULTIPLAYERRACE;
    gDifficulty = DIFFICULTY_MEDIUM;
    unlockedAges = 0;
    gNetPort = 0;
    NSpGame* host = NSpGame_Host();
    CHECK(host);
    gNetPort = ntohs(Address(host->hostListenSocket).sin_port);
    memset(peers, 0, MAX_CLIENTS * sizeof(*peers));
    peers[kNSpHostID] = host;
    for (int id = 1; id < numPlayers; id++)
    {
        peers[id] = Join(host);
        CHECK(peers[id]->myID == id);
    }
    for (int id = 1; id < numPlayers; id++)
        Drain(peers[id]); // announcements of later joiners
    gNetGame = host;
    for (int i = 0; i < gNumTotalPlayers; i++)
    {
        gPlayerInfo[i].net.nspPlayerID = PlayerNSpID(numPlayers, i);
        gPlayerInfo[i].health = 1;
    }
    return host;
}

static void DisposeClients(NSpGame* peers[MAX_CLIENTS])
{
    for (int id = 1; id < MAX_CLIENTS; id++)
    {
        if (peers[id])
            NSpGame_Dispose(peers[id], 0);
        peers[id] = NULL;
    }
}

static void EndSession(NSpGame* peers[MAX_CLIENTS])
{
    DisposeClients(peers);
    NSpGame_Dispose(peers[kNSpHostID], 0);
    peers[kNSpHostID] = NULL;
    gNetGame = NULL;
}

static void Readiness(uint32_t timeout, int waiting, int ready)
{
    NSpGame* peers[MAX_CLIENTS];
    NSpGame* host = BeginSession(SMALL_SESSION, peers);
    if (waiting == kNetSequence_HostWaitForPlayersToPrepareLevel)
        InitPlayersAtStartOfLevel();
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
    CHECK(!gPlayerInfo[2].isEliminated); // the replacement must retain car controls
    CHECK(gNumGatheredPlayers == 2);
    CHECK(gNumPlayersEliminated == 0); // race bots do not affect battle bookkeeping
    CHECK(ExpectLeave(peers[2]) == 1);
    EndSession(peers);
}

static void LastPeerTimeout(void)
{
    NSpGame* peers[MAX_CLIENTS];
    NSpGame* host = BeginSession(SMALL_SESSION, peers);
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
    DisposeClients(peers);
}

static void SelectorResumesAfterTeardown(void)
{
    NSpGame* peers[MAX_CLIENTS];
    NSpGame* host = BeginSession(SMALL_SESSION, peers);
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
    DisposeClients(peers);
}

static void DelayedReady(void)
{
    NSpGame* peers[MAX_CLIENTS];
    NSpGame* host = BeginSession(SMALL_SESSION, peers);
    ClearPlayerSyncMask();
    MarkPlayerSynced(0);
    MarkPlayerSynced(1);
    gNetSequenceState = kNetSequence_HostWaitForPlayersToPrepareLevel;
    testNow += LEVEL_READY_TIMEOUT_MS - 1;
    MarkPlayerSynced(2);
    HostAdvanceReadinessBarrier(LEVEL_READY_TIMEOUT_MS,
        kNetSequence_HostWaitForPlayersToPrepareLevel, kNetSequence_GameLoop);
    CHECK(gNetSequenceState == kNetSequence_GameLoop && NSpGame_GetNumActivePlayers(host) == 3);
    EndSession(peers);
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
    NSpGame* peers[MAX_CLIENTS];
    BeginSession(SMALL_SESSION, peers);
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
    NSpMessageHeader* leave = WaitMessage(peers[2]);
    CHECK(leave->what == kNSpPlayerLeft);
    memcpy(gPlayerInfo, before, sizeof(before));
    gNumPlayersEliminated = alreadyEliminated ? 1 : 0;
    gNumGatheredPlayers = 3;
    gTrackCompleted = false;
    gIsNetworkHost = false;
    gIsNetworkClient = true;
    gNetGame = peers[2];
    gNetSequenceState = kNetSequence_ClientWaitForSyncFromHost;
    CHECK(!HandleOtherNetMessage(leave));
    NSpMessage_Release(peers[2], leave);
    ExpectSurvivalWinner();
    EndSession(peers);
}

static void BattleDepartureWinner(int mode)
{
    NSpGame* peers[MAX_CLIENTS];
    BeginSession(SMALL_SESSION, peers);
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
    EndSession(peers);
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
    NSpGame* peers[MAX_CLIENTS];
    BeginSession(SMALL_SESSION, peers);
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

    NSpMessageHeader* leave = WaitMessage(peers[2]);
    CHECK(leave->what == kNSpPlayerLeft);
    memcpy(gPlayerInfo, before, sizeof(before));
    gNumPlayersEliminated = 0;
    gNumGatheredPlayers = 3;
    gTrackCompleted = false;
    gIsNetworkHost = false;
    gIsNetworkClient = true;
    gNetGame = peers[2];
    gNetSequenceState = kNetSequence_WaitingForPlayerVehicles;
    CHECK(!HandleOtherNetMessage(leave));
    NSpMessage_Release(peers[2], leave);
    VerifyVehicleTimeoutAfterInit(mode);
    EndSession(peers);
}

static void SurvivalWithoutSurvivors(void)
{
    NSpGame* peers[MAX_CLIENTS];
    BeginSession(SMALL_SESSION, peers);
    gGameMode = GAME_MODE_SURVIVAL;
    for (int i = 0; i < gNumTotalPlayers; i++) PlayerLoseHealth(i, 1);
    UpdateGameModeSpecifics();
    CHECK(gTrackCompleted && gNumPlayersEliminated == gNumTotalPlayers);
    for (int i = 0; i < gNumTotalPlayers; i++)
        CHECK(winLoseWinner[i] == -1 && winLoseMode[i] == 2);
    EndSession(peers);
}

static void NetworkReplacementVehicle(int selectedVehicle)
{
    NSpGame* peers[MAX_CLIENTS];
    BeginSession(SMALL_SESSION, peers);
    gPlayerInfo[2].vehicleType = selectedVehicle;
    ApplyBecomeBot(2);
    const int difficulties[] = {DIFFICULTY_MEDIUM, DIFFICULTY_HARD};
    for (int d = 0; d < 2; d++)
    {
        gDifficulty = difficulties[d];
        for (int ages = 0; ages <= 2; ages += 2)
        {
            unlockedAges = ages; // peers may have different local tournament saves
            gPlayerInfo[2].vehicleType = selectedVehicle;
            InitPlayersAtStartOfLevel();
            CHECK(gPlayerInfo[2].vehicleType == selectedVehicle);
            CHECK(gPlayerInfo[2].isComputer && !gPlayerInfo[2].isEliminated);
        }
    }
    EndSession(peers);
}

static void TagWinnerDeparture(void)
{
    NSpGame* peers[MAX_CLIENTS];
    BeginSession(SMALL_SESSION, peers);
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
    EndSession(peers);
}

static void PausedLeaveAndReset(void)
{
    NSpGame* peers[MAX_CLIENTS];
    BeginSession(SMALL_SESSION, peers);
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
    EndSession(peers);
}

// A join beyond capacity is answered with a reason, not silently dropped, and does
// not disturb any seated peer.
static void ExpectJoinRefused(NSpGame* host)
{
    uint32_t seated = NSpGame_GetActivePlayersIDMask(host);
    LobbyInfo lobby = {.hostAddr = Address(host->hostListenSocket)};
    NSpGame* extra = JoinLobby(&lobby);
    CHECK(extra);
    CHECK(AcceptClient(host) == -1);
    NSpMessageHeader* denied = WaitMessage(extra);
    CHECK(denied->what == kNSpJoinDenied);
    CHECK(!strcmp(((NSpJoinDeniedMessage*) denied)->reason, "THE GAME IS FULL."));
    NSpMessage_Release(extra, denied);
    NSpGame_Dispose(extra, 0);
    CHECK(NSpGame_GetActivePlayersIDMask(host) == seated);
    CHECK(!NSpMessage_Get(host));
}

static uint32_t ExpectLeaves(NSpGame* game, int count)
{
    uint32_t heard = 0;
    for (int i = 0; i < count; i++)
        heard |= 1u << ExpectLeave(game);
    CHECK(!NSpMessage_Get(game));
    return heard;
}

// Close every odd client ID (sparse, non-adjacent departures).
static uint32_t DisposeOddClients(NSpGame* peers[MAX_CLIENTS], int* count)
{
    uint32_t odd = 0;
    *count = 0;
    for (int id = 1; id < FULL_SESSION; id += 2)
    {
        NSpGame_Dispose(peers[id], 0);
        peers[id] = NULL;
        odd |= 1u << id;
        (*count)++;
    }
    return odd;
}

// Seat every client slot and refuse one more; then free sparse slots and refill them.
static void CapacityLobby(void)
{
    NSpGame* peers[MAX_CLIENTS];
    NSpGame* host = BeginSession(FULL_SESSION, peers);
    CHECK(NSpGame_GetNumActivePlayers(host) == FULL_SESSION);
    for (int id = 0; id < FULL_SESSION; id++)
        CHECK(NSpGame_GetActivePlayersIDMask(peers[id]) == AllPeersMask(FULL_SESSION));
    ExpectJoinRefused(host);

    int leavers;
    uint32_t odd = DisposeOddClients(peers, &leavers);
    CHECK(ExpectLeaves(host, leavers) == odd);
    CHECK(NSpGame_GetActivePlayersIDMask(host) == (AllPeersMask(FULL_SESSION) & ~odd));
    for (int id = 2; id < FULL_SESSION; id += 2)
        CHECK(ExpectLeaves(peers[id], leavers) == odd);

    // Rejoins take the lowest free IDs, so the table is full again.
    for (int id = 1; id < FULL_SESSION; id += 2)
    {
        peers[id] = Join(host);
        CHECK(peers[id]->myID == id);
    }
    for (int id = 1; id < FULL_SESSION; id++)
        Drain(peers[id]);
    for (int id = 0; id < FULL_SESSION; id++)
        CHECK(NSpGame_GetActivePlayersIDMask(peers[id]) == AllPeersMask(FULL_SESSION));
    ExpectJoinRefused(host);
    EndSession(peers);
}

// The real configuration broadcast numbers every seated client densely.
static void CapacityConfig(void)
{
    NSpGame* peers[MAX_CLIENTS];
    BeginSession(FULL_SESSION, peers);
    gTheAge = 0;
    gTrackNum = 0;
    gTargetFPS = 60;
    CHECK(HostSendGameConfigInfo() == noErr);
    CHECK(gNumRealPlayers == FULL_SESSION && gMyNetworkPlayerNum == 0);
    uint32_t numbered = 1;
    for (int id = 1; id < FULL_SESSION; id++)
    {
        NSpMessageHeader* message = WaitMessage(peers[id]);
        CHECK(message->what == kNetConfigureMessage);
        const NetConfigMessage* config = (const NetConfigMessage*) message;
        CHECK(NetValidateConfigPayload(config) && config->numPlayers == FULL_SESSION);
        CHECK(config->playerNum > 0 && gPlayerInfo[config->playerNum].net.nspPlayerID == id);
        numbered |= 1u << config->playerNum;
        NSpMessage_Release(peers[id], message);
    }
    CHECK(numbered == AllPeersMask(FULL_SESSION));
    EndSession(peers);
}

static void SendVehicle(NSpGame* client, int playerNum)
{
    NetPlayerCharTypeMessage message;
    memset(&message, 0, sizeof(message));
    NSpClearMessageHeader(&message.h);
    message.h.to = kNSpAllPlayers;
    message.h.what = kNetPlayerCharTypeMessage;
    message.h.messageLen = sizeof(message);
    message.playerNum = playerNum;
    message.vehicleType = playerNum % NUM_LAND_CAR_TYPES;
    message.sex = playerNum & 1;
    message.skin = playerNum % NUM_CAVEMAN_SKINS;
    CHECK(NSpMessage_Send(client, &message.h, kNSpSendFlag_Registered) == kNSpRC_OK);
}

static void SendLevelReady(NSpGame* client)
{
    NetSyncMessage message;
    memset(&message, 0, sizeof(message));
    NSpClearMessageHeader(&message.h);
    message.h.to = kNSpHostID;
    message.h.what = kNetSyncMessage;
    message.h.messageLen = sizeof(message);
    CHECK(NSpMessage_Send(client, &message.h, kNSpSendFlag_Registered) == kNSpRC_OK);
}

static void PumpHostUntil(int state)
{
    for (int i = 0; i < 1000 && gNetSequenceState != state; i++)
    {
        if (!UpdateNetSequence())
            SDL_Delay(1);
    }
    CHECK(gNetSequenceState == state);
}

// Read exactly the relayed vehicle choices and leave notices a client is owed.
static void ExpectRelays(NSpGame* client, int vehicles, uint32_t leaves, int leaveCount)
{
    uint32_t heard = 0;
    for (int pending = vehicles + leaveCount; pending > 0; pending--)
    {
        NSpMessageHeader* message = WaitMessage(client);
        if (message->what == kNetPlayerCharTypeMessage)
            vehicles--;
        else
        {
            CHECK(message->what == kNSpPlayerLeft);
            heard |= 1u << ((NSpPlayerLeftMessage*) message)->playerID;
        }
        NSpMessage_Release(client, message);
    }
    CHECK(vehicles == 0 && heard == leaves && !NSpMessage_Get(client));
}

// Both readiness barriers complete with every client seat in use, and with sparse
// clients leaving before they report: exactly those players become race bots.
static void CapacityReadiness(bool departures)
{
    NSpGame* peers[MAX_CLIENTS];
    NSpGame* host = BeginSession(FULL_SESSION, peers);
    ClearPlayerSyncMask();
    MarkPlayerSynced(kNSpHostID);
    gNetSequenceState = kNetSequence_WaitingForPlayerVehicles;
    int leavers = 0;
    uint32_t leaving = departures ? DisposeOddClients(peers, &leavers) : 0;
    int senders = 0;
    for (int id = 1; id < FULL_SESSION; id++)
    {
        if (peers[id])
        {
            SendVehicle(peers[id], NSpIDPlayer(FULL_SESSION, id));
            senders++;
        }
    }
    PumpHostUntil(kNetSequence_GotAllPlayerVehicles);
    CHECK(!gGameOver && gNumGatheredPlayers == FULL_SESSION - leavers);
    CHECK(NSpGame_GetActivePlayersIDMask(host) == (AllPeersMask(FULL_SESSION) & ~leaving));
    for (int id = 1; id < FULL_SESSION; id++)
    {
        int playerNum = NSpIDPlayer(FULL_SESSION, id);
        const PlayerInfoType* player = &gPlayerInfo[playerNum];
        if (leaving & (1u << id))
            CHECK(player->isComputer && !player->isEliminated);
        else
            CHECK(!player->isComputer && player->vehicleType == playerNum % NUM_LAND_CAR_TYPES);
    }
    for (int id = 1; id < FULL_SESSION; id++)
    {
        if (peers[id])
            ExpectRelays(peers[id], senders - 1, leaving, leavers);
    }

    ClearPlayerSyncMask();
    MarkPlayerSynced(kNSpHostID);
    gNetSequenceState = kNetSequence_HostWaitForPlayersToPrepareLevel;
    for (int id = 1; id < FULL_SESSION; id++)
    {
        if (peers[id])
            SendLevelReady(peers[id]);
    }
    PumpHostUntil(kNetSequence_GameLoop);
    CHECK(gPlayerSyncMask == NSpGame_GetActivePlayersIDMask(host));
    EndSession(peers);
}

static void PumpHostUntilScheduled(int events)
{
    for (int i = 0; i < 1000; i++)
    {
        Host_PumpClientInputs();
        int scheduled = 0;
        for (int s = 0; s < NET_MAX_PENDING_EVENTS; s++)
            scheduled += sHostPendingEvents[s].active;
        if (scheduled == events)
            return;
        CHECK(scheduled < events);
        SDL_Delay(1);
    }
    DoFatalAlert("Timed out waiting for %d scheduled leave events", events);
}

// Every client may leave a running race in the same frame: each leave becomes one
// frame-aligned bot conversion, and one host packet carries all of them.
static void CapacityInGameDepartures(void)
{
    NSpGame* peers[MAX_CLIENTS];
    BeginSession(FULL_SESSION, peers);
    gNetSequenceState = kNetSequence_GameLoop;
    gHostSendCounter = 10;
    int leavers;
    uint32_t odd = DisposeOddClients(peers, &leavers);
    PumpHostUntilScheduled(leavers);
    for (int id = 2; id < FULL_SESSION; id += 2)
    {
        CHECK(ExpectLeaves(peers[id], leavers) == odd);
        CHECK(!gPlayerInfo[NSpIDPlayer(FULL_SESSION, id)].isComputer);
    }
    DisposeClients(peers);
    PumpHostUntilScheduled(FULL_SESSION - 1);

    NetHostControlInfoMessageType wire;
    memset(&wire, 0, sizeof(wire));
    uint32_t frame = gHostSendCounter + NET_MAX_EVENT_LEAD;
    Host_FillOutgoingEvents(&wire, gHostSendCounter);
    CHECK(wire.eventCount == FULL_SESSION - 1);
    uint32_t converting = 0;
    for (int e = 0; e < wire.eventCount; e++)
    {
        CHECK(wire.events[e].type == kEvBecomeBot && wire.events[e].effectiveFrame == frame);
        converting |= 1u << wire.events[e].playerNum;
    }
    CHECK(converting == (AllPeersMask(FULL_SESSION) & ~1u));
    CHECK(gNumGatheredPlayers == FULL_SESSION && !gGameOver);
    gHostSendCounter = frame + 1; // the host just sent the effective frame
    ApplyPendingFrameEvents();
    for (int i = 1; i < FULL_SESSION; i++)
        CHECK(gPlayerInfo[i].isComputer && !gPlayerInfo[i].isEliminated);
    CHECK(gNumGatheredPlayers == 1 && gGameOver);
    CHECK(gNetSequenceState == kNetSequence_OfflineEverybodyLeft);
    EndSession(peers);
}

// --join-address connects straight to the host's listener instead of searching the
// LAN, then performs the ordinary join handshake; a missing host is reported as such.
static void DirectJoin(void)
{
    ResetNetGameTransientState();
    gNetPort = 0;
    NSpGame* host = NSpGame_Host();
    CHECK(host);
    gNetPort = ntohs(Address(host->hostListenSocket).sin_port);
    gCommandLine.netJoin = gCommandLine.netJoinDirect = true;
    gCommandLine.netJoinAddress = INADDR_LOOPBACK;
    expectedGatherState = kNetSequence_ClientJoiningGame;
    gatherScreenCalls = 0;
    CHECK(SetupNetworkJoin()); // the stub gather screen cancels after one call
    CHECK(gatherScreenCalls == 1 && gNetGame && !gNetSearch);
    CHECK(!gCommandLine.netJoinDirect); // consumed: a later menu join searches the LAN
    NSpGame* client = gNetGame;
    CHECK(AcceptClient(host) == 1);
    NSpMessageHeader* request = WaitMessage(host);
    CHECK(request->what == kNSpJoinRequest);
    CHECK(NSpGame_AckJoinRequest(host, request) == kNSpRC_OK);
    NSpMessage_Release(host, request);
    NSpMessageHeader* approved = WaitMessage(client);
    CHECK(approved->what == kNSpJoinApproved && client->myID == 1);
    NSpMessage_Release(client, approved);
    NSpGame_Dispose(client, 0);
    CHECK(ExpectLeave(host) == 1);
    NSpGame_Dispose(host, 0); // nothing listens on this port any more

    expectedGatherState = kNetSequence_ClientOfflineBecauseHostUnreachable;
    gatherScreenCalls = 0;
    gNetGame = NULL;
    gCommandLine.netJoinDirect = true;
    CHECK(SetupNetworkJoin());
    CHECK(gatherScreenCalls == 1 && !gNetGame && !gNetSearch);
    memset(&gCommandLine, 0, sizeof(gCommandLine));
}

// Smoke-only auto-start (--smoke-net-players): the host starts once the expected players
// joined and the extra join the smoke test sends was refused, and never without the options.
static void SmokeLobbyAutoStart(void)
{
    NSpGame* peers[MAX_CLIENTS];
    NSpGame* host = BeginSession(FULL_SESSION - 1, peers);
    gNetSequenceState = kNetSequence_HostLobbyOpen;
    gCommandLine.smokeTestFrames = 1;
    gCommandLine.smokeNetPlayers = FULL_SESSION;
    gCommandLine.smokeNetRefusals = 1;
    CHECK(DoNetGatherControls() == 0 && gNetSequenceState == kNetSequence_HostLobbyOpen);

    peers[FULL_SESSION - 1] = Join(host); // every seat is taken now
    for (int id = 1; id < FULL_SESSION; id++)
        Drain(peers[id]);
    CHECK(DoNetGatherControls() == 0 && gNetSequenceState == kNetSequence_HostLobbyOpen);
    CHECK(NSpGame_GetNumRefusedClients(host) == 0);
    ExpectJoinRefused(host);
    CHECK(NSpGame_GetNumRefusedClients(host) == 1);

    memset(&gCommandLine, 0, sizeof(gCommandLine)); // normal play waits for the host to confirm
    CHECK(DoNetGatherControls() == 0 && gNetSequenceState == kNetSequence_HostLobbyOpen);
    gCommandLine.smokeTestFrames = 1;
    gCommandLine.smokeNetPlayers = FULL_SESSION;
    gCommandLine.smokeNetRefusals = 1;
    CHECK(DoNetGatherControls() == 0 && gNetSequenceState == kNetSequence_HostReadyToStartGame);
    memset(&gCommandLine, 0, sizeof(gCommandLine));
    EndSession(peers);
}

// One human and a full grid of CPUs in a local practice race.
static void BeginLocalPractice(void)
{
    memset(gPlayerInfo, 0, sizeof(gPlayerInfo));
    gNetGameInProgress = gIsNetworkHost = gIsNetworkClient = false;
    gGameMode = GAME_MODE_PRACTICE;
    gNumRealPlayers = 1;
    gNumTotalPlayers = MAX_PLAYERS;
    for (int i = 1; i < gNumTotalPlayers; i++) gPlayerInfo[i].isComputer = true;
    unlockedAges = 0;
    gDifficulty = DIFFICULTY_MEDIUM;
}

// Local games pick CPU vehicles through the extracted picker, in player order.
static void LocalCPUVehicles(void)
{
    static const short freeCars[] = {CAR_TYPE_TURTLE, CAR_TYPE_LOG, CAR_TYPE_GEODE,
        CAR_TYPE_BONEBUGGY, CAR_TYPE_MAMMOTH}; // best starter cars first, skipping the human's
    BeginLocalPractice();
    gPlayerInfo[0].vehicleType = CAR_TYPE_ROCK;
    InitPlayersAtStartOfLevel();
    CHECK(gPlayerInfo[0].vehicleType == CAR_TYPE_ROCK);
    for (int i = 1; i < gNumTotalPlayers; i++)
        CHECK(gPlayerInfo[i].vehicleType == freeCars[(i - 1) % 5]);
    gDifficulty = DIFFICULTY_HARD; // the RandomRange stub returns its minimum
    InitPlayersAtStartOfLevel();
    for (int i = 1; i < gNumTotalPlayers; i++)
        CHECK(gPlayerInfo[i].vehicleType == CAR_TYPE_MAMMOTH);
}

// Each player's level-start height is sampled at its own start coordinate.
static void StartHeightSampling(void)
{
    BeginLocalPractice();
    for (int i = 0; i < gNumTotalPlayers; i++)
    {
        gPlayerInfo[i].startX = 1000 * (i + 1);
        gPlayerInfo[i].startZ = -700 * (i + 1);
    }
    numTerrainQueries = 0;
    InitPlayersAtStartOfLevel();
    CHECK(numTerrainQueries == gNumTotalPlayers);
    for (int i = 0; i < gNumTotalPlayers; i++)
        CHECK(terrainQueries[i].x == gPlayerInfo[i].startX && terrainQueries[i].y == gPlayerInfo[i].startZ);
}

// A local split-screen race: humans take the first slots and panes, and with CPU fill
// CPU cars (not on this machine, no pane) take the rest, on the best cars left free.
static void LocalSplitScreenSeats(void)
{
    for (short humans = 2; humans <= MAX_LOCAL_PLAYERS; humans++)
    {
        for (int fill = 0; fill <= 1; fill++)
        {
            memset(gPlayerInfo, 0, sizeof(gPlayerInfo));
            gNetGameInProgress = gIsNetworkHost = gIsNetworkClient = false;
            gGameMode = GAME_MODE_MULTIPLAYERRACE;
            gNumLocalPlayers = gNumRealPlayers = humans;
            gCPUFillThisRace = fill;
            gMyNetworkPlayerNum = 0;
            InitPlayerInfo_Game();
            CHECK(gNumTotalPlayers == (fill ? MAX_PLAYERS : humans));
            for (int i = 0; i < gNumTotalPlayers; i++)
            {
                Boolean human = i < humans;
                CHECK(gPlayerInfo[i].isComputer == !human);
                CHECK(gPlayerInfo[i].onThisMachine == human);
                CHECK(gPlayerInfo[i].splitPaneNum == (human ? i : -1));
                if (human)
                {
                    gPlayerInfo[i].vehicleType = CAR_TYPE_ROCK - i;    // the best starter cars
                    gPlayerInfo[i].sex = 1;                             // every human picks the same driver
                }
            }

            unlockedAges = 0;
            gDifficulty = DIFFICULTY_MEDIUM;
            InitPlayersAtStartOfLevel();
            const int numFreeCars = CAR_TYPE_ROCK + 1 - humans;      // the starter cars no human drives
            for (int i = humans; i < gNumTotalPlayers; i++)          // best first, one each, then again
            {
                CHECK(gPlayerInfo[i].vehicleType == CAR_TYPE_ROCK - humans - (i - humans) % numFreeCars);
                for (int human = 0; human < humans; human++)         // and no CPU driver looks like a human
                    CHECK(gPlayerInfo[i].sex != gPlayerInfo[human].sex || gPlayerInfo[i].skin != gPlayerInfo[human].skin);
            }
        }
    }
    gCPUFillThisRace = false;
    gNumLocalPlayers = gNumRealPlayers = 1;
}

// A network race with CPU fill: every peer seats the same CPU cars. They depend only on
// the humans' choices, the track and the difficulty: not on local unlocks, not on whether
// this peer saw a departure before level start, and never on the synced RNG.
static void NetworkFillVehicles(void)
{
    static const short humanCars[SMALL_SESSION] = {CAR_TYPE_CHARIOT, CAR_TYPE_ROCK, CAR_TYPE_OBELISK};
    short picks[2][2][2][MAX_PLAYERS];
    for (int hard = 0; hard <= 1; hard++)
    {
        for (int ages = 0; ages <= 1; ages++)
        {
            for (int departed = 0; departed <= 1; departed++)
            {
                NSpGame* peers[MAX_CLIENTS];
                BeginSession(SMALL_SESSION, peers);
                gCPUFillThisRace = true;
                gMyNetworkPlayerNum = 0;
                InitPlayerInfo_Game();
                CHECK(gNumTotalPlayers == MAX_PLAYERS);
                for (int i = 0; i < SMALL_SESSION; i++)
                    gPlayerInfo[i].vehicleType = humanCars[i];
                if (departed)
                    ApplyBecomeBot(2); // this peer processed player 2's leave before level start
                gDifficulty = hard ? DIFFICULTY_HARD : DIFFICULTY_MEDIUM;
                unlockedAges = ages ? NUM_AGES : 0; // peers may have different tournament saves
                gTrackNum = 3;
                randomRangeCalls = 0;
                InitPlayersAtStartOfLevel();
                CHECK(randomRangeCalls == 0);
                for (int i = 0; i < gNumTotalPlayers; i++)
                {
                    CHECK(gPlayerInfo[i].isComputer == (i >= SMALL_SESSION || (departed && i == 2)));
                    if (i < SMALL_SESSION)
                        CHECK(gPlayerInfo[i].vehicleType == humanCars[i]); // a replacement keeps its car
                    CHECK(gPlayerInfo[i].vehicleType >= 0 && gPlayerInfo[i].vehicleType < NUM_LAND_CAR_TYPES);
                    picks[hard][ages][departed][i] = gPlayerInfo[i].vehicleType;
                }
                EndSession(peers);
            }
        }
        for (int view = 1; view < 4; view++)
            CHECK(!memcmp(picks[hard][0][0], picks[hard][view / 2][view % 2], sizeof(picks[hard][0][0])));
    }

    // Below Hard: the best cars of the whole roster that no human drives, best first,
    // then the same order again.
    short bestFree[NUM_LAND_CAR_TYPES];
    int numFree = 0;
    for (int type = NUM_LAND_CAR_TYPES - 1; type >= 0; type--)
    {
        if (type != humanCars[0] && type != humanCars[1] && type != humanCars[2])
            bestFree[numFree++] = type;
    }
    CHECK(bestFree[0] == CAR_TYPE_CATAPULT && bestFree[1] == CAR_TYPE_TROJANHORSE && bestFree[2] == CAR_TYPE_TURTLE);
    for (int i = SMALL_SESSION; i < MAX_PLAYERS; i++)
        CHECK(picks[0][0][0][i] == bestFree[(i - SMALL_SESSION) % numFree]);
    gCPUFillThisRace = false;
    gNumLocalPlayers = gNumRealPlayers = 1;
}

// Network fill CPUs look the same on every peer, whatever each peer's character screen
// swapped into their slots, and unlike any human while looks are left.
static void NetworkFillLooks(void)
{
    short looks[2][MAX_PLAYERS][2];
    for (int view = 0; view < 2; view++)
    {
        NSpGame* peers[MAX_CLIENTS];
        BeginSession(SMALL_SESSION, peers);
        gCPUFillThisRace = true;
        gMyNetworkPlayerNum = view; // the host, or the client in slot 1
        InitPlayerInfo_Game();
        for (int i = 0; i < SMALL_SESSION; i++)
        {
            gPlayerInfo[i].vehicleType = CAR_TYPE_MAMMOTH;
            gPlayerInfo[i].sex = 0; // network players may all pick the same driver
            gPlayerInfo[i].skin = 3;
        }
        for (int i = SMALL_SESSION; i < MAX_PLAYERS; i++)
            gPlayerInfo[i].skin = (i + view) % NUM_CAVEMAN_SKINS; // this peer's screen swapped outfits
        InitPlayersAtStartOfLevel();
        for (int i = 0; i < MAX_PLAYERS; i++)
        {
            looks[view][i][0] = gPlayerInfo[i].sex;
            looks[view][i][1] = gPlayerInfo[i].skin;
            if (i < SMALL_SESSION)
                CHECK(gPlayerInfo[i].sex == 0 && gPlayerInfo[i].skin == 3);
            else if (MAX_PLAYERS - SMALL_SESSION + 1 <= 2 * NUM_CAVEMAN_SKINS) // looks are left
                CHECK(gPlayerInfo[i].sex != 0 || gPlayerInfo[i].skin != 3);
        }
        EndSession(peers);
    }
    CHECK(!memcmp(looks[0], looks[1], sizeof(looks[0])));
    gCPUFillThisRace = false;
    gNumLocalPlayers = gNumRealPlayers = 1;
    gMyNetworkPlayerNum = 0;
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
    NetworkReplacementVehicle(CAR_TYPE_MAMMOTH); // the shared default before selection
    NetworkReplacementVehicle(CAR_TYPE_GEODE); // a selection already received from the peer
    TagWinnerDeparture();
    PausedLeaveAndReset();
    CapacityLobby();
    CapacityConfig();
    CapacityReadiness(false);
    CapacityReadiness(true);
    CapacityInGameDepartures();
    DirectJoin();
    SmokeLobbyAutoStart();
    LocalCPUVehicles();
    StartHeightSampling();
    LocalSplitScreenSeats();
    NetworkFillVehicles();
    NetworkFillLooks();
    puts("Readiness, paused-leave, full-lobby, local CPU vehicle, start-height, split-screen seat and network fill tests passed");
    return 0;
}
