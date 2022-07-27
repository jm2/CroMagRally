//
// network.h
//

#include "main.h"
#include "netsprocket.h"


enum
{
	kNetConfigureMessage			= 'ncfg',
	kNetPlayerCharTypeMessage		= 'type',
	kNetSyncMessage					= 'sync',
	kNetHostControlInfoMessage		= 'hctl',
	kNetClientControlInfoMessage	= 'cctl',
};

		/***************************/
		/* MESSAGE DATA STRUCTURES */
		/***************************/

		/* GAME CONFIGURATION MESSAGE */

typedef struct
{
	NSpMessageHeader	h;
	int32_t				gameMode;							// game mode (tag, race, etc.)
	int32_t				age;								// which age to play for race mode
	int32_t				trackNum;							// which track to play for battle modes
	int32_t				playerNum;							// this player's index
	int32_t				numPlayers;							// # players in net game
	int16_t				numTracksCompleted;					// pass saved game value to clients so we're all the same here
	int16_t				difficulty;							// pass host's difficulty setting so we're in sync
	int16_t				tagDuration;						// # minutes in tag game
}NetConfigMessage;

		/* SYNC MESSAGE */

typedef struct
{
	NSpMessageHeader	h;
}NetSyncMessage;


		/* HOST CONTROL INFO MESSAGE */

typedef struct
{
	NSpMessageHeader	h;
	float				fps, fpsFrac;
	uint32_t			randomSeed;					// simply used for error checking (all machines should have same seed!)
	uint32_t			controlBits[MAX_PLAYERS];
	uint32_t			controlBitsNew[MAX_PLAYERS];
	OGLVector2D			analogSteering[MAX_PLAYERS];
	uint32_t			frameCounter;
	uint8_t				pauseState[MAX_PLAYERS];

#if _DEBUG
	OGLPoint3D			playerPositionCheck[MAX_PLAYERS];		// additional error checking in debug mode
#endif
}NetHostControlInfoMessageType;


		/* CLIENT CONTROL INFO MESSAGE */

typedef struct
{
	NSpMessageHeader	h;
	int16_t				playerNum;
	uint32_t			controlBits;
	uint32_t			controlBitsNew;
	uint32_t			frameCounter;
	OGLVector2D			analogSteering;
	uint8_t				pauseState;
}NetClientControlInfoMessageType;


		/* PLAYER CHAR TYPE MESSAGE */

typedef struct
{
	NSpMessageHeader	h;
	int16_t				playerNum;
	int16_t				vehicleType;
	int16_t				sex;				// 0 = male, 1 = female
	int16_t				skin;
}NetPlayerCharTypeMessage;


//===============================================================================


void InitNetworkManager(void);
void ShutdownNetworkManager(void);
Boolean SetupNetworkHosting(void);
Boolean SetupNetworkJoin(void);
void ClientTellHostLevelIsPrepared(void);
void HostWaitForPlayersToPrepareLevel(void);

void HostSend_ControlInfoToClients(void);
void ClientSend_ControlInfoToHost(void);
Boolean ClientReceive_ControlInfoFromHost(void);
void HostReceive_ControlInfoFromClients(void);

void PlayerBroadcastVehicleType(void);
void GetVehicleSelectionFromNetPlayers(void);


void EndNetworkGame(void);

//===============================================================================

enum
{
	kNetSequence_Offline,
	kNetSequence_Error,
	kNetSequence_ClientOfflineBecauseHostBailed,
	kNetSequence_ClientOfflineBecauseHostUnreachable,
	kNetSequence_ClientOfflineBecauseKicked,

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
};


bool UpdateNetSequence(void);

Boolean IsNetGamePaused(void);
