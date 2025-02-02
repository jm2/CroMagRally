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
#include <SDL.h>

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

#if _DEBUG
	gHostOutMess.simTick		= gSimulationFrame;
#endif

	for (i = 0; i < MAX_PLAYERS; i++)								// control bits
	{
		gHostOutMess.controlBits[i] = gPlayerInfo[i].controlBits;
		gHostOutMess.controlBitsNew[i] = gPlayerInfo[i].controlBits_New;
		gHostOutMess.analogSteering[i] = gPlayerInfo[i].analogSteering;
		gHostOutMess.pauseState[i] = gPlayerInfo[i].net.pauseState;

#if _DEBUG
		if (!gPlayerInfo[i].headObj)
			gHostOutMess.playerPositionCheck[i] = (OGLPoint3D) {0,0,0};
		else
			gHostOutMess.playerPositionCheck[i] = gPlayerInfo[i].coord;
#endif
	}

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

	gTimeoutCounter = 0;

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

#if _DEBUG
	if (mess->simTick != gSimulationFrame)
	{
		DoFatalAlert("Sim tick mismatch! mine=%u host=%u", mess->simTick, gSimulationFrame);
	}
#endif

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

#if _DEBUG
		if (gPlayerInfo[i].headObj)
		{
			GAME_ASSERT_MESSAGE(
					0 == memcmp(&mess->playerPositionCheck[i], &gPlayerInfo[i].coord, sizeof(OGLPoint3D)),
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
Boolean								gotIt = false;

	GAME_ASSERT(gIsNetworkClient);

	tick = TickCount();														// init tick for timeout

	while (!gotIt)
	{
		inMess = NSpMessage_Get(gNetGame);									// get message

		if (inMess)
		{
				/* WE GOT A PACKET */

			switch (inMess->what)
			{
				case kNetHostControlInfoMessage:
					gotIt = true;
					Client_InGame_HandleHostControlInfoMessage((NetHostControlInfoMessageType*) inMess);
					break;

				default:
					if (HandleOtherNetMessage(inMess))
					{
						gotIt = true;
					}
					break;
			}

			NSpMessage_Release(gNetGame, inMess);
			inMess = NULL;
		}
		else
		{
				/* SEE IF WE ARE NOT GETTING THE PACKET */
				//
				// If this happens, then it is possible that Net Sprocket lost a packet.  There is no way to know who's packet got lost
				// so go ahead and send our most recent packet again in case it was us.  The Host will throw out any dupes that it gets.
				//
				// [SOURCE PORT NOTE: I don't think we should resend packets since we're using TCP for everything]

			if ((TickCount() - tick) > (DATA_TIMEOUT*60))	// see if we've been waiting longer than n seconds
			{
				gTimeoutCounter++;								// keep track of how often this happens
				if (gTimeoutCounter > 3)
				{
					NetGameFatalError(kNetSequence_ErrorNoResponseFromHost);
					return;
				}

#if 0	// SOURCE PORT: DON'T RESEND - We're using TCP!
				NSpMessage_Send(gNetGame, &gClientOutMess.h, kNSpSendFlag_Registered);	// resend the last message
#endif

				tick = TickCount();														// reset tick
			}
		}
	}
}



/************** CLIENT SEND CONTROL INFO TO HOST *********************/
//
// At the end of each frame, the client sends the new control state info to the host for
// the next frame.
//

void ClientSend_ControlInfoToHost(void)
{
OSStatus						status;

	GAME_ASSERT(gIsNetworkClient);

				/* BUILD MESSAGE */

	NSpClearMessageHeader(&gClientOutMess.h);

	gClientOutMess.h.to 			= kNSpHostID;							// send to Host
	gClientOutMess.h.what 			= kNetClientControlInfoMessage;			// set message type
	gClientOutMess.h.messageLen 	= sizeof(gClientOutMess);				// set size of message

	gClientOutMess.frameCounter		= gClientSendCounter[gMyNetworkPlayerNum]++;	// send client frame counter & inc
	gClientOutMess.playerNum		= gMyNetworkPlayerNum;
	gClientOutMess.controlBits 		= gPlayerInfo[gMyNetworkPlayerNum].controlBits;
	gClientOutMess.controlBitsNew  	= gPlayerInfo[gMyNetworkPlayerNum].controlBits_New;
	gClientOutMess.analogSteering	= gPlayerInfo[gMyNetworkPlayerNum].analogSteering;
	gClientOutMess.pauseState		= gPlayerInfo[gMyNetworkPlayerNum].net.pauseState;

			/* SEND IT */

	status = NSpMessage_Send(gNetGame, &gClientOutMess.h, kNSpSendFlag_Registered);
//	if (status)
//		DoFatalAlert("ClientSend_ControlInfoToHost: NSpMessage_Send failed!");
}


/*************** HOST GET CONTROL INFO FROM CLIENTS ***********************/

static Boolean Host_InGame_HandleClientControlInfoMessage(NetClientControlInfoMessageType* mess)
{
	GAME_ASSERT(gIsNetworkHost);

	int i = mess->playerNum;								// get player #

	if (mess->frameCounter < gClientSendCounter[i])			// see if this is an old packet, possibly a duplicate.  If so, skip it
		return false;
	if (mess->frameCounter > gClientSendCounter[i])			// see if we skipped a packet; one must have gotten lost
		DoFatalAlert("HostReceive_ControlInfoFromClients: It seems Net Sprocket has lost a packet");
	gClientSendCounter[i]++;								// inc counter since the next packet we get will be +1


	gPlayerInfo[i].controlBits	= mess->controlBits;
	gPlayerInfo[i].controlBits_New = mess->controlBitsNew;
	gPlayerInfo[i].analogSteering = mess->analogSteering;
	gPlayerInfo[i].net.pauseState = mess->pauseState;

	return true;
}

void HostReceive_ControlInfoFromClients(void)
{
NSpMessageHeader 					*inMess;
uint32_t								tick;
Boolean								abort = false;

	GAME_ASSERT(gIsNetworkHost);

	ClearPlayerSyncMask();
	MarkPlayerSynced(NSpPlayer_GetMyID(gNetGame));			// the host already has its own info

	tick = TickCount();										// start tick for timeout

	while (!AreAllPlayersSynced() && !abort)				// loop until I've got the message from all players
	{
		inMess = NSpMessage_Get(gNetGame);					// get message
		if (inMess)
		{
			tick = TickCount();								// reset tick for timeout
			switch(inMess->what)
			{
				case kNetClientControlInfoMessage:
					if (Host_InGame_HandleClientControlInfoMessage((NetClientControlInfoMessageType*) inMess))
					{
						MarkPlayerSynced(inMess->from);
					}
					break;

				default:
					if (HandleOtherNetMessage(inMess))
					{
						abort = true;
					}
					break;
			}
			NSpMessage_Release(gNetGame, inMess);			// dispose of message
		}

		if ((TickCount() - tick) > (DATA_TIMEOUT*60))		// see if we've been waiting longer than n seconds
		{
			gTimeoutCounter++;								// keep track of how often this happens
			if (gTimeoutCounter > 3)
			{
				NetGameFatalError(kNetSequence_ErrorNoResponseFromClients);
				return;
			}

#if 0		// Resending this while the client is paused will throw them out of sync!!
			NSpMessage_Send(gNetGame, &gHostOutMess.h, kNSpSendFlag_Registered);
#endif
			tick = TickCount();														// reset tick
		}
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
	gNumGatheredPlayers--;										// one less net player in the game
	gNumRealPlayers--;

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
