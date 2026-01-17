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
#include "miscscreens.h"
#include <stdlib.h>
#include <stdio.h>
#include <SDL3/SDL.h>

/**********************/
/*     PROTOTYPES     */
/**********************/

static OSErr HostSendGameConfigInfo(void);
static void HandleGameConfigMessage(NetConfigMessage *inMessage);
static Boolean HandleOtherNetMessage(NSpMessageHeader	*message);
static void PlayerUnexpectedlyLeavesGame(NSpPlayerLeftMessage *mess);

/****************************/
/*    CONSTANTS             */
/****************************/

#define LOADING_TIMEOUT	15						// # seconds to wait for clients to load a level
#define	DATA_TIMEOUT	2						// # seconds for data to timeout

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
int				gTimeoutCounter;


NetHostControlInfoMessageType	gHostOutMess;
NetClientControlInfoMessageType	gClientOutMess;

// ============================================================================
// HIGH-FREQUENCY NETWORK BUFFERS
// ============================================================================

#define INPUT_QUEUE_SIZE 256
#define INPUT_QUEUE_MASK (INPUT_QUEUE_SIZE - 1)

// Client Jitter Buffers (One per player, filled by Host updates)
static NetInputFrame s_ClientInputQueue[MAX_PLAYERS][INPUT_QUEUE_SIZE];
static int s_ClientQueueHead[MAX_PLAYERS];
static int s_ClientQueueTail[MAX_PLAYERS];

// Host Jitter Buffers (One per player, filled by Client updates)
static NetInputFrame s_HostInputQueue[MAX_PLAYERS][INPUT_QUEUE_SIZE];
static int s_HostQueueHead[MAX_PLAYERS];
static int s_HostQueueTail[MAX_PLAYERS];

// Local Batch Accumulator (Waiting to be sent to Host)
static NetInputUnit s_LocalBatch[NET_BATCH_SIZE];
static int s_LocalBatchCount = 0;
// FORWARD DECLARATIONS
static Boolean Host_InGame_HandleClientControlInfoMessage(NetClientControlInfoMessageType* mess);

static uint32_t s_LocalBatchStartFrame = 0;

// Host Batch Accumulator (Waiting to be sent to Clients)
// Host must batch inputs for ALL players.
static NetInputUnit s_HostBatch[MAX_PLAYERS][NET_BATCH_SIZE];
static int s_HostBatchCount = 0;
static uint32_t s_HostBatchStartFrame = 0;
static float s_HostLastFPS = 60.0f;
static float s_HostLastFPSFrac = 0.016f;

// ----------------------------------------------------------------------------
// QUEUE HELPERS
// ----------------------------------------------------------------------------

static void Queue_Append(NetInputFrame* queue, int* head, int* tail, NetInputFrame* item)
{
    queue[*tail] = *item;
    *tail = (*tail + 1) & INPUT_QUEUE_MASK;
    
    // Overflow check: if tail hits head, drop the oldest item (head++)
    if (*tail == *head)
    {
        *head = (*head + 1) & INPUT_QUEUE_MASK;
        printf("NETWORK WARNING: Input Queue Overflow! Dropping frame.\n");
    }
}

static Boolean Queue_Pop(NetInputFrame* queue, int* head, int* tail, NetInputFrame* outItem)
{
    if (*head == *tail) return false; // Empty
    
    *outItem = queue[*head];
    *head = (*head + 1) & INPUT_QUEUE_MASK;
    return true;
}

static int Queue_Count(int head, int tail)
{
    return (tail - head) & INPUT_QUEUE_MASK;
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

static void MarkPlayerSynced(NSpPlayerID playerID)
{
	gPlayerSyncMask |= 1 << playerID;
}

static Boolean AreAllPlayersSynced(void)
{
	uint32_t targetMask = NSpGame_GetActivePlayersIDMask(gNetGame);
	return 0 == (gPlayerSyncMask ^ targetMask);
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

void ResetNetworkQueues(void)
{
    SDL_memset(s_ClientQueueHead, 0, sizeof(s_ClientQueueHead));
    SDL_memset(s_ClientQueueTail, 0, sizeof(s_ClientQueueTail));
    
    SDL_memset(s_HostQueueHead, 0, sizeof(s_HostQueueHead));
    SDL_memset(s_HostQueueTail, 0, sizeof(s_HostQueueTail));
    
    s_LocalBatchCount = 0;
    s_HostBatchCount = 0;
    s_LocalBatchStartFrame = 0;
    s_HostBatchStartFrame = 0;
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
OSErr	iErr;

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
	gHostSendCounter	= 0;
	memset(gClientSendCounter, 0, sizeof(gClientSendCounter));
}


#pragma mark - Net sequence

/****************** NETWORK SEQUENCE *********************/

bool UpdateNetSequence(void)
{
	NSpMessageHeader* message = NULL;

	switch (gNetSequenceState)
	{
		case kNetSequence_HostLobbyOpen:
		{
			if (kNSpRC_OK != NSpGame_AdvertiseTick(gNetGame, gFramesPerSecondFrac))
			{
				gNetSequenceState = kNetSequence_Error;
				break;
			}

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
			NSpGame_StopAdvertising(gNetGame);
			NSpGame_StopAcceptingNewClients(gNetGame);
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
			}
			break;

		case kNetSequence_ClientJoiningGame:
		{
			message = NSpMessage_Get(gNetGame);

			if (message) switch (message->what)
			{
				case	kNetConfigureMessage:										// GOT GAME START INFO
					HandleGameConfigMessage((NetConfigMessage*) message);
					gNetSequenceState = kNetSequence_ClientJoinedGame;
					break;

//				case 	kNSpGameTerminated:											// Host terminated the game :(
//					break;

				case	kNSpJoinApproved:
				{
					NSpJoinApprovedMessage* approvedMessage = (NSpJoinApprovedMessage*) message;
					printf("Join approved! My player ID is %d\n", approvedMessage->header.to);
					break;
				}

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

						// TODO: Check player num

						gPlayerInfo[mess->playerNum].vehicleType = mess->vehicleType;	// save this player's type
						gPlayerInfo[mess->playerNum].sex = mess->sex;					// save this player's sex
						gPlayerInfo[mess->playerNum].skin = mess->skin;					// save this player's skin
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


#pragma mark - Host/Join


/****************** SETUP NETWORK HOSTING *********************/
//
// Called when this computer's user has selected to be a host for a net game.
//
// OUTPUT:  true == cancelled.
//
Boolean SetupNetworkHosting(void)
{
	gNetSequenceState = kNetSequence_HostOffline;


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
		gNetSequenceState = kNetSequence_Error;
		// Don't goto failure; show the error to the player
		DoNetGatherScreen();
		return true;
	}

	if (kNSpRC_OK != NSpGame_StartAdvertising(gNetGame))
	{
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

	return true;
}


/*************** SETUP NETWORK JOIN ************************/
//
// OUTPUT:	false == let's go!
//			true = cancel
//

Boolean SetupNetworkJoin(void)
{
OSStatus			status = noErr;

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

	return DoNetGatherScreen();
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

void HandleGameConfigMessage(NetConfigMessage* inMessage)
{
	gGameMode 			= inMessage->gameMode;
	gTheAge 			= inMessage->age;
	gTrackNum 			= inMessage->trackNum;
	gNumRealPlayers 	= inMessage->numPlayers;
	gMyNetworkPlayerNum = inMessage->playerNum;

	// Copy transient settings
	gTagDuration = inMessage->tagDuration;
	gDifficulty = inMessage->difficulty;
//	gTransientNumTracksCompleted = inMessage->numTracksCompleted;

	// Get NSp's playerIDs (for use when player leaves game)
	for (int i = 0; i < gNumRealPlayers; i++)
	{
		gPlayerInfo[i].net.nspPlayerID = NSpGame_GetNthActivePlayerID(gNetGame, i);
	}
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


// ----------------------------------------------------------------------------
// HIGH-FREQUENCY API (Called by Main.c)
// ----------------------------------------------------------------------------

void Net_Client_AccumulateInput(uint32_t controlBits, uint32_t controlBitsNew, OGLVector2D analogSteering, uint8_t pauseState)
{
    if (!gNetGameInProgress) return; // Allow both Host and Client

    // 1. Add to local batch
    s_LocalBatch[s_LocalBatchCount].controlBits = controlBits;
    s_LocalBatch[s_LocalBatchCount].controlBitsNew = controlBitsNew;
    s_LocalBatch[s_LocalBatchCount].analogSteering = analogSteering;
    
    // We update the global pause state immediately so it's fresh when we flush
    gPlayerInfo[gMyNetworkPlayerNum].net.pauseState = pauseState; 
    
    s_LocalBatchCount++;
    
    // 2. If full, send
    if (s_LocalBatchCount >= NET_BATCH_SIZE)
    {
        ClientSend_ControlInfoToHost();
        s_LocalBatchCount = 0;
        s_LocalBatchStartFrame += NET_BATCH_SIZE;
    }
}

Boolean Net_GetNextSimulationFrame(NetInputFrame* outInputs)
{
    // Retrieve one frame of input for ALL players
    for (int i=0; i < gNumRealPlayers; i++)
    {
        NSpPlayerID pid = gPlayerInfo[i].net.nspPlayerID; // Map to index? No, gPlayerInfo IS the index map.
        
        // Host uses s_HostInputQueue, Client uses s_ClientInputQueue
        NetInputFrame* q = gIsNetworkHost ? s_HostInputQueue[i] : s_ClientInputQueue[i];
        int* head = gIsNetworkHost ? &s_HostQueueHead[i] : &s_ClientQueueHead[i];
        int* tail = gIsNetworkHost ? &s_HostQueueTail[i] : &s_ClientQueueTail[i];
        
        if (!Queue_Pop(q, head, tail, &outInputs[i]))
        {
             static uint32_t lastLog = 0;
             if (SDL_GetTicks() - lastLog > 1000) {
                 SDL_Log("Stall! Waiting for Player %d (Host=%d, Head=%d, Tail=%d)", 
                    i, gIsNetworkHost, *head, *tail);
                 lastLog = SDL_GetTicks();
             }
            return false; // Stutter / buffer empty
        }
    }
    return true;
}


/************** GET NETWORK CONTROL INFO FROM HOST *********************/

static Boolean Client_InGame_HandleHostControlInfoMessage(NetHostControlInfoMessageType* mess)
{
	GAME_ASSERT(gIsNetworkClient);

	gTimeoutCounter = 0;

    // Unlike before, we don't reject "old" packets by counter because batches might arrive out of order (UDP) 
    // but we use TCP/Registered so order is guaranteed.
    // However, we DO need to ensure we don't process a duplicate.
    
	if (mess->frameCounter < gHostSendCounter)			
		return false;

	if (mess->frameCounter > gHostSendCounter)			
	{
		// Gap in frames? Should not happen with TCP.
        // But if it does, we are in trouble.
		NetGameFatalError(kNetSequence_ErrorLostPacket);
		return false;
	}

    // Advance expected frame counter by number of inputs in batch
	gHostSendCounter += mess->numInputs; 

	gFramesPerSecond 		= mess->fps;
	gFramesPerSecondFrac 	= mess->fpsFrac;

	if (MyRandomLong() != mess->randomSeed)				// verify that host's random # is in sync with ours!
	{
		NetGameFatalError(kNetSequence_SeedDesync);
		return false;
	}
    
    // Unpack Batch into Queue
    int numInputs = mess->numInputs;
    if (numInputs > NET_BATCH_SIZE) numInputs = NET_BATCH_SIZE;
    
	for (int p = 0; p < MAX_PLAYERS; p++)
	{
        for (int i = 0; i < numInputs; i++)
        {
            NetInputFrame frame;
            frame.controlBits       = mess->inputs[p][i].controlBits;
            frame.controlBitsNew    = mess->inputs[p][i].controlBitsNew;
            frame.analogSteering    = mess->inputs[p][i].analogSteering;
            frame.pauseState        = mess->pauseState[p]; // Pause state is per-packet, applied to all frames
            
            Queue_Append(s_ClientInputQueue[p], &s_ClientQueueHead[p], &s_ClientQueueTail[p], &frame);
        }

#if _DEBUG
		if (gPlayerInfo[p].headObj)
		{
			GAME_ASSERT_MESSAGE(
					0 == memcmp(&mess->playerPositionCheck[p], &gPlayerInfo[p].coord, sizeof(OGLPoint3D)),
					"Player positions got out of sync!");
		}
#endif
	}

	return true;
}

void ClientReceive_ControlInfoFromHost(void)
{
NSpMessageHeader 					*inMess = NULL;
uint32_t							tick;
//Boolean								gotIt = false;

	GAME_ASSERT(gIsNetworkClient);

    // Drain the socket until empty.
    // We want to fill our Jitter Buffer as much as possible.
	while (true)
	{
		inMess = NSpMessage_Get(gNetGame);
		if (!inMess) break;

        switch (inMess->what)
        {
            case kNetHostControlInfoMessage:
                Client_InGame_HandleHostControlInfoMessage((NetHostControlInfoMessageType*) inMess);
                break;

            default:
                HandleOtherNetMessage(inMess);
                break;
        }

        NSpMessage_Release(gNetGame, inMess);
	}
}


/**************** CLIENT CHECK IF PACKET WAITING *****************/
Boolean Client_IsPacketWaiting(void)
{
    // Deprecated in new architecture. The "Packet" is now a batch in the queue.
    // We return true if the queue has data.
    return (Queue_Count(s_ClientQueueHead[0], s_ClientQueueTail[0]) > 0);
}


/**************** CLIENT CONSUME NEXT PACKET IF AVAILABLE *****************/
Boolean Client_ConsumeNextPacketIfAvailable(void)
{
    // Deprecated. Just call Receive to drain socket.
    ClientReceive_ControlInfoFromHost();
    return false; 
}


// ----------------------------------------------------------------------------
// CLIENT SEND CONTROL INFO TO HOST
// ----------------------------------------------------------------------------

void ClientSend_ControlInfoToHost(void)
{
OSStatus status;

    // GAME_ASSERT(gIsNetworkClient); // Removed, Host calls this too

    NSpClearMessageHeader(&gClientOutMess.h);

    gClientOutMess.h.to             = kNSpHostID;                           // send to Host
    gClientOutMess.h.what           = kNetClientControlInfoMessage;         // set message type
    gClientOutMess.h.messageLen     = sizeof(gClientOutMess);               // set size of message
    
    gClientOutMess.frameCounter     = s_LocalBatchStartFrame;
    gClientOutMess.numInputs        = s_LocalBatchCount;
    gClientOutMess.playerNum        = gMyNetworkPlayerNum;
    gClientOutMess.pauseState       = gPlayerInfo[gMyNetworkPlayerNum].net.pauseState;
    
    // Copy batch
    for (int i = 0; i < s_LocalBatchCount; i++)
    {
        gClientOutMess.inputs[i] = s_LocalBatch[i];
    }

    // SEND IT (or LOOPBACK)
    if (gIsNetworkHost)
    {
        // Direct injection for Host (Loopback)
        Host_InGame_HandleClientControlInfoMessage(&gClientOutMess);
    }
    else
    {
        status = NSpMessage_Send(gNetGame, &gClientOutMess.h, kNSpSendFlag_Registered);
        if (status)
            NetGameFatalError(kNetSequence_ErrorSendFailed);
    }
}


// ----------------------------------------------------------------------------
// HOST BATCH API
// ----------------------------------------------------------------------------

void Net_Host_AccumulateBatch(NetInputFrame* frames) 
{
    // Store inputs for ALL players into the host batch
    for (int i = 0; i < MAX_PLAYERS; i++)
    {
        s_HostBatch[i][s_HostBatchCount].controlBits     = frames[i].controlBits;
        s_HostBatch[i][s_HostBatchCount].controlBitsNew  = frames[i].controlBitsNew;
        s_HostBatch[i][s_HostBatchCount].analogSteering  = frames[i].analogSteering;
    }
    
    // Also track pause state? Host tracks it separately.
    
    s_HostBatchCount++;
    
    if (s_HostBatchCount >= NET_BATCH_SIZE)
    {
        HostSend_ControlInfoToClients();
        s_HostBatchCount = 0;
        s_HostBatchStartFrame += NET_BATCH_SIZE;
    }
}


/************** SEND HOST CONTROL INFO TO CLIENTS *********************/

void HostSend_ControlInfoToClients(void)
{
OSStatus						status;

	GAME_ASSERT(gIsNetworkHost);

	NSpClearMessageHeader(&gHostOutMess.h);

	gHostOutMess.h.to 			= kNSpAllPlayers;						// send to all clients
	gHostOutMess.h.what 		= kNetHostControlInfoMessage;			// set message type
	gHostOutMess.h.messageLen 	= sizeof(gHostOutMess);						// set size of message

	gHostOutMess.frameCounter	= s_HostBatchStartFrame;
	
	// We send the *average* FPS over the batch? Or just current.
	// Since physics is fixed, this is mostly for debug or pure render.
	gHostOutMess.fps 			= s_HostLastFPS;						
	gHostOutMess.fpsFrac		= s_HostLastFPSFrac;
	
	gHostOutMess.randomSeed		= MyRandomLong();
    gHostOutMess.numInputs      = s_HostBatchCount;

    // Copy batch
    for (int p=0; p < MAX_PLAYERS; p++)
    {
        for (int i=0; i < s_HostBatchCount; i++)
        {
            gHostOutMess.inputs[p][i] = s_HostBatch[p][i];
        }
		gHostOutMess.pauseState[p] = gPlayerInfo[p].net.pauseState;
    }
    
    // TODO: SimTick and PositionCheck

			/* SEND IT */

	status = NSpMessage_Send(gNetGame, &gHostOutMess.h, kNSpSendFlag_Registered);
	if (status)
		NetGameFatalError(kNetSequence_ErrorSendFailed);
}


/*************** HOST GET CONTROL INFO FROM CLIENTS ***********************/

static Boolean Host_InGame_HandleClientControlInfoMessage(NetClientControlInfoMessageType* mess)
{
    int playerNum = mess->playerNum;
    if (playerNum < 0 || playerNum >= MAX_PLAYERS) return false;
    
    int numInputs = mess->numInputs;
    if (numInputs > NET_BATCH_SIZE) numInputs = NET_BATCH_SIZE;
    
    // Unpack Batch into Host Queue
    for (int i = 0; i < numInputs; i++)
    {
        NetInputFrame frame;
        frame.controlBits       = mess->inputs[i].controlBits;
        frame.controlBitsNew    = mess->inputs[i].controlBitsNew;
        frame.analogSteering    = mess->inputs[i].analogSteering;
        frame.pauseState        = mess->pauseState;
        
        Queue_Append(s_HostInputQueue[playerNum], &s_HostQueueHead[playerNum], &s_HostQueueTail[playerNum], &frame);
    }
    
    return true;
}

void HostReceive_ControlInfoFromClients(void)
{
    NSpMessageHeader* inMess;
    // We do NOT block anymore. We just drain the network buffer.
    // If the queue is empty, the Simulation loop will handle the stall.

	GAME_ASSERT(gIsNetworkHost);
    
    while(true)
    {
        inMess = NSpMessage_Get(gNetGame);
        if (!inMess) break;
        
        switch(inMess->what)
        {
            case kNetClientControlInfoMessage:
                if (Host_InGame_HandleClientControlInfoMessage((NetClientControlInfoMessageType*) inMess))
                {
                    MarkPlayerSynced(inMess->from); // keep stats happy
                }
                break;

            default:
                HandleOtherNetMessage(inMess);
                break;
        }
        NSpMessage_Release(gNetGame, inMess);
    }
}


#pragma mark -


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

			/* SEND IT */

	status = NSpMessage_Send(gNetGame, &outMess.h, kNSpSendFlag_Registered);
	if (status)
	{
		NetGameFatalError(kNetSequence_ErrorSendFailed);
		return;
	}
}

/***************** GET VEHICLE SELECTION FROM NET PLAYERS ***********************/

void GetVehicleSelectionFromNetPlayers(void)
{
	ClearPlayerSyncMask();
	MarkPlayerSynced(NSpPlayer_GetMyID(gNetGame));		// we have our own local info already

	gNetSequenceState = kNetSequence_WaitingForPlayerVehicles;
	DoNetGatherScreen();
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
				DoFatalAlert("HandleOtherNetMessage: kNSpError");


					/* A PLAYER UNEXPECTEDLY HAS LEFT THE GAME */

		case	kNSpPlayerLeft:
				PlayerUnexpectedlyLeavesGame((NSpPlayerLeftMessage *)message);
				if (gGameOver)
				{
					gNetSequenceState = kNetSequence_OfflineEverybodyLeft;
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

		case	kNSpJoinRequest:
				DoFatalAlert("HandleOtherNetMessage: kNSpJoinRequest");

		case	kNSpJoinApproved:
				DoFatalAlert("HandleOtherNetMessage: kNSpJoinApproved");

		case	kNSpJoinDenied:
				DoFatalAlert("HandleOtherNetMessage: kNSpJoinDenied");

		case	kNSpPlayerJoined:
				DoFatalAlert("HandleOtherNetMessage: kNSpPlayerJoined");

		case	kNSpHostChanged:
				DoFatalAlert("HandleOtherNetMessage: kNSpHostChanged");

		case	kNSpGroupCreated:
				DoFatalAlert("HandleOtherNetMessage: kNSpGroupCreated");

		case	kNSpGroupDeleted:
				DoFatalAlert("HandleOtherNetMessage: kNSpGroupDeleted");

		case	kNSpPlayerAddedToGroup:
				DoFatalAlert("HandleOtherNetMessage: kNSpPlayerAddedToGroup");

		case	kNSpPlayerRemovedFromGroup:
				DoFatalAlert("HandleOtherNetMessage: kNSpPlayerAddedToGroup");

		case	kNSpPlayerTypeChanged:
				DoFatalAlert("HandleOtherNetMessage: kNSpPlayerAddedToGroup");

		default:
				DoFatalAlert("HandleOtherNetMessage: unknown");
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
int			i;
NSpPlayerID	playerID = mess->playerID;

	i = FindHumanByNSpPlayerID(playerID);

	if (i < 0)
	{
		DoFatalAlert("PlayerUnexpectedlyLeavesGame: cannot find matching player id#");
	}


	gPlayerInfo[i].isComputer = true;							// turn it into a computer player.
	gPlayerInfo[i].isEliminated = true;							// also eliminate from battles
	gPlayerInfo[i].net.pauseState = 0;							// unpause if they were paused
	gNumGatheredPlayers--;										// one less net player in the game
	// gNumRealPlayers--;										// DON'T decrement this, or the screen layout will change mid-game!

	if (gNumRealPlayers <= 1)									// see if nobody to play with
		gGameOver = true;

			/* HANDLE SPECIFICS */

	switch(gGameMode)
	{
		case	GAME_MODE_TAG1:
		case	GAME_MODE_TAG2:
				if (gPlayerInfo[i].isIt)
					ChooseTaggedPlayer();
				break;
	}
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
