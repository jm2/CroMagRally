/****************************/
/*   	PLAYER.C   			*/
/* (c)2000 Pangea Software  */
/* By Brian Greenstone      */
/****************************/


/****************************/
/*    EXTERNALS             */
/****************************/

#include "game.h"
#include "cpu_driver.h"
#include "cpu_fill.h"
#include "vehicle_picker.h"
#include "driver_looks.h"

/****************************/
/*    PROTOTYPES            */
/****************************/

static uint16_t SyncedCPUVehicleRandom(void* context, int cpuIndex, uint16_t min, uint16_t max);
static void GetPlayerDriverLooks(DriverLook looks[MAX_PLAYERS]);
static void SetPlayerDriverLooks(const DriverLook looks[MAX_PLAYERS]);


/****************************/
/*    CONSTANTS             */
/****************************/


/*********************/
/*    VARIABLES      */
/*********************/


short		gNumRealPlayers = 1;                // # of actual human players (on computer and/or network)
short       gNumTotalPlayers = 0;               // # of total player characters in game (real + CPU)
short		gNumLocalPlayers = 1;				// 2 if split-screen, otherwise 1

ObjNode		*gCurrentPlayer;
short		gCurrentPlayerNum = 0;				// current player number of player being processed (via MovePlayer et. al.)
short		gMyNetworkPlayerNum = 0;			// not the same as the ClientID, just a player number that the host assigns.  The host is always player #0

PlayerInfoType	gPlayerInfo[MAX_PLAYERS];

OGLColorRGB	gTagColor;

/******************** INIT PLAYER INFO ***************************/
//
// Called once at beginning of game (after network has been setup if needed).
//

void InitPlayerInfo_Game(void)
{
short	i;

	// Back up gPlayerInfo before zeroing out the structs.
	// HostSendGameConfigInfo is called prior to this function and it sets some network info we'll need.
	PlayerInfoType* backup = (PlayerInfoType*) AllocPtr(sizeof(gPlayerInfo));
	memcpy(backup, gPlayerInfo, sizeof(gPlayerInfo));

	memset(gPlayerInfo, 0, sizeof(gPlayerInfo));		// init everything to 0

	for (i = 0; i < MAX_PLAYERS; i++)
	{
		const DriverLook defaultLook = GetDefaultDriverLook(i);

		gPlayerInfo[i].objNode			= nil;

		gPlayerInfo[i].sex 				= defaultLook.sex;	// alternate male/female, swapped in each further wave of 6

		gPlayerInfo[i].startX 			= 0;
		gPlayerInfo[i].startZ 			= 0;
		gPlayerInfo[i].coord.x 			= 0;
		gPlayerInfo[i].coord.y 			= 0;
		gPlayerInfo[i].coord.z 			= 0;
		gPlayerInfo[i].onThisMachine 	= false;

		gPlayerInfo[i].wheelObj[0] 		=
		gPlayerInfo[i].wheelObj[1] 		=
		gPlayerInfo[i].wheelObj[2] 		=
		gPlayerInfo[i].wheelObj[3] 		=
		gPlayerInfo[i].headObj = nil;



		gPlayerInfo[i].splitPaneNum		= -1;


		gPlayerInfo[i].team = i % 2;			// set team for capture the flag mode


		if (gGameMode == GAME_MODE_CAPTUREFLAG)
		{
			gPlayerInfo[i].skin = gPlayerInfo[i].team == RED_TEAM ? CAVEMAN_SKIN_RED : CAVEMAN_SKIN_GREEN;
		}
		else
		{
			gPlayerInfo[i].skin = defaultLook.skin;
		}


				/* CAR SPECS INFO */

		gPlayerInfo[i].steering					= 0;




			/* AI */

		gPlayerInfo[i].isComputer 	    = true;


			/* NET */

		gPlayerInfo[i].net = backup[i].net;
	}

			/* NETWORK GAME */

	if (gNetGameInProgress)
	{
		gPlayerInfo[gMyNetworkPlayerNum].onThisMachine = true;			// set the local player
		gPlayerInfo[gMyNetworkPlayerNum].splitPaneNum = 0;
	}

			/* LOCAL GAME */
	else
	{
		for (i = 0; i < gNumLocalPlayers; i++)
		{
			gPlayerInfo[i].onThisMachine = true;
			gPlayerInfo[i].splitPaneNum  = i;
		}
	}


	    /* REAL PLAYERS ARE NOT CONTROLLED BY CPU */

	for (i = 0; i < gNumRealPlayers; i++)
	{
		gPlayerInfo[i].isComputer 	    = false;        // these are real players, not computer players
    }


			/* SEE HOW MANY PLAYERS IN GAME */
			//
			// CPU cars (isComputer above) take every slot after the humans'. Multiplayer
			// races only have them with CPU fill; battle modes never do.
			//

	gNumTotalPlayers = CountPlayersInGame(gGameMode, gNumRealPlayers, gCPUFillThisRace, gPlayerLimitThisGame);
	if (gCommandLine.smokeCars && (gGameMode == GAME_MODE_PRACTICE || gGameMode == GAME_MODE_TOURNAMENT))
		gNumTotalPlayers = gCommandLine.smokeCars;			// smoke soak: --smoke-cars sets the field size


	SafeDisposePtr((Ptr) backup);
	backup = NULL;
}


/******************* INIT PLAYERS AT START OF LEVEL ********************/
//
// Initializes player stuff at the beginning of each track.
//

void InitPlayersAtStartOfLevel(void)
{
int		i,j;
int		numCPUVehiclesPicked = 0;
CPUVehiclePickRules	cpuVehicleRules = { .randomRange = SyncedCPUVehicleRandom };
SharedCPUVehicleSeed	sharedCPUVehicleSeed;

	gWorstHumanPlace = 0;
	gNumPlayersEliminated = 0;
	const Boolean battleMode = gGameMode == GAME_MODE_TAG1 || gGameMode == GAME_MODE_TAG2
		|| gGameMode == GAME_MODE_SURVIVAL || gGameMode == GAME_MODE_CAPTUREFLAG;


		/* FIRST MARK WHICH CAR TYPES THE HUMANS HAVE */

	for (i = 0; i < gNumTotalPlayers; i++)
	{
		if (!gPlayerInfo[i].isComputer)								// check for human player
		{
			GAME_ASSERT(gPlayerInfo[i].vehicleType >= 0);
			GAME_ASSERT(gPlayerInfo[i].vehicleType < NUM_LAND_CAR_TYPES);
			cpuVehicleRules.humanCarMask |= 1u << gPlayerInfo[i].vehicleType;	// mark this used
		}
	}

	cpuVehicleRules.agesCompleted = GetNumAgesCompleted();
	cpuVehicleRules.difficulty = gDifficulty;


		/* NETWORK CPU FILL CARS COME FROM SHARED STATE */
		//
		// Every peer must seat the same cars, so they depend only on what the peers
		// share: the humans' choices (including players who left since), the track and
		// the difficulty. Never on local unlocks or the synced RNG.
		//

	if (gNetGameInProgress)
	{
		short	humanCars[MAX_PLAYERS];

		for (i = 0; i < gNumRealPlayers; i++)
			humanCars[i] = gPlayerInfo[i].vehicleType;

		InitSharedCPUVehiclePickRules(&cpuVehicleRules, &sharedCPUVehicleSeed,
				humanCars, gNumRealPlayers, gDifficulty, gTrackNum);
	}


		/* DONT DRESS A CPU LIKE A HUMAN OR ANOTHER CPU */
		//
		// Humans (including network players who have since become bots) keep the look they
		// chose. This reads only state every network peer shares, so they all dress the CPUs
		// alike. Capture the Flag outfits are team colours, so they repeat on purpose.
		//

	if (gNetGameInProgress && gCPUFillThisRace)						// start every peer's fill CPUs from the same looks
		DressNetworkFillCPUs(gPlayerInfo, gNumRealPlayers, gNumTotalPlayers);

	if (gGameMode != GAME_MODE_CAPTUREFLAG)
	{
		DriverLook	looks[MAX_PLAYERS];

		GetPlayerDriverLooks(looks);
		ResolveCPUDriverLooks(looks, gNumTotalPlayers, gNumRealPlayers);
		SetPlayerDriverLooks(looks);
	}


			/* SET SOME GLOBALS */

	for (i = 0; i < gNumTotalPlayers; i++)
	{
		// Network replacements (human slots) retain the shared selection (or its default);
		// network CPU fill cars (every slot after the humans') get the shared picks above.
		// Pick in player order: local Hard draws stay interleaved with SetPhysicsForVehicleType's.
		if (gPlayerInfo[i].isComputer && (!gNetGameInProgress || i >= gNumRealPlayers))	// set CPU vehicle type
			gPlayerInfo[i].vehicleType = PickCPUVehicle(&cpuVehicleRules, numCPUVehiclesPicked++);

		gPlayerInfo[i].coord.y = GetTerrainY(gPlayerInfo[i].startX,gPlayerInfo[i].startZ);

			/* CREATE THE CAR MODEL */

		if (gTrackNum == TRACK_NUM_ATLANTIS)
			InitPlayer_Submarine(i, &gPlayerInfo[i].coord, gPlayerInfo[i].startRotY);
		else
			InitPlayer_Car(i, &gPlayerInfo[i].coord, gPlayerInfo[i].startRotY);

		gPlayerInfo[i].objNode->InvincibleTimer = 0;

		gPlayerInfo[i].controlBits		= 0;
		gPlayerInfo[i].controlBits_New	= 0;
		gPlayerInfo[i].analogSteering.x	= 0;
		gPlayerInfo[i].analogSteering.y	= 0;

		gPlayerInfo[i].distToFloor				= 0;
		gPlayerInfo[i].skidDot					= 0;
		gPlayerInfo[i].mostRecentFloorY 		= 0;

		gPlayerInfo[i].onWater			 		= false;
		gPlayerInfo[i].waterY 					= 0;

		gPlayerInfo[i].lapNum					= -1;			// start @ -1 since we cross the finish line @ start
		gPlayerInfo[i].checkpointNum			= gNumCheckpoints-1;
		gPlayerInfo[i].place					= i;
		gPlayerInfo[i].distToNextCheckpoint		= 0;
		gPlayerInfo[i].raceComplete				= false;
		gPlayerInfo[i].cheated					= false;

		gPlayerInfo[i].snowParticleGroup		= -1;
		gPlayerInfo[i].snowTimer				= 0;
		gPlayerInfo[i].frozenTimer				= 0;


		for (j = 0; j < MAX_CHECKPOINTS; j++)				// start with all checkpoints tagged to trick lapNum @ start of race
			gPlayerInfo[i].checkpointTagged[j] 	= true;

		gPlayerInfo[i].currentThrust	= 0;
		gPlayerInfo[i].gasPedalDown		= false;
		gPlayerInfo[i].accelBackwards	= false;
		gPlayerInfo[i].movingBackwards	= false;
		gPlayerInfo[i].braking			= false;
		gPlayerInfo[i].isPlaning		= false;
		gPlayerInfo[i].greasedTiresTimer = 0;
		gPlayerInfo[i].wrongWay			= false;
		gPlayerInfo[i].steering			= 0;
		gPlayerInfo[i].currentRPM		= 0;
		gPlayerInfo[i].submarineImmobilized = 0;
		gPlayerInfo[i].bumpSoundTimer	= 0;

			/* DRAG DEBRIS */

		gPlayerInfo[i].tiresAreDragging		= false;
		gPlayerInfo[i].alwaysDoDrag			= false;
		gPlayerInfo[i].dragDebrisTimer		= 0;
		gPlayerInfo[i].dragDebrisParticleGroup = -1;
		gPlayerInfo[i].dragDebrisMagicNum 	= 0;
		gPlayerInfo[i].dragDebrisTexture	= 0;

		gPlayerInfo[i].lastSkidSegCoord.x 	=
		gPlayerInfo[i].lastSkidSegCoord.y 	= 0;
		gPlayerInfo[i].lastSkidVector.x 	=
		gPlayerInfo[i].lastSkidVector.y 	= 0;
		gPlayerInfo[i].skidSmokeParticleGroup = -1;
		gPlayerInfo[i].skidSmokeMagicNum 	= 0;
		gPlayerInfo[i].skidSmokeTimer		= 0;
		gPlayerInfo[i].makingSkid			= false;
		gPlayerInfo[i].skidChannel			= -1;
		gPlayerInfo[i].skidSoundTimer		= 0;

		gPlayerInfo[i].skidColor.r 			=
		gPlayerInfo[i].skidColor.g 			=
		gPlayerInfo[i].skidColor.b 			=
		gPlayerInfo[i].skidColor.a 			= 0;

				/* POWERUP */

		gPlayerInfo[i].nitroTimer			= 0;
		gPlayerInfo[i].stickyTiresTimer		= 0;
		gPlayerInfo[i].superSuspensionTimer	= 0;
		gPlayerInfo[i].numTokens			= 0;
		gPlayerInfo[i].invisibilityTimer	= 0;
		gPlayerInfo[i].flamingTimer			= 0;

				/* BATTLE MODES */

		gPlayerInfo[i].tagTimer				= TAG_TIME_LIMIT;
		gPlayerInfo[i].tagOccilation		= 0;
		gPlayerInfo[i].isIt					= false;
		// Battle modes have no CPU entrants. A network bot here is a peer who
		// left during vehicle selection; level initialization must not revive it.
		gPlayerInfo[i].isEliminated = gNetGameInProgress && battleMode && gPlayerInfo[i].isComputer;
		if (gPlayerInfo[i].isEliminated
			&& (gGameMode == GAME_MODE_TAG1 || gGameMode == GAME_MODE_SURVIVAL))
			gNumPlayersEliminated++;
		gPlayerInfo[i].health				= 1.0;
		gPlayerInfo[i].impactResetTimer		= 0;

			/* GROUND TILE INFO */

		gPlayerInfo[i].groundTraction		= 1.0;
		gPlayerInfo[i].groundFriction		= 1.0;
		gPlayerInfo[i].groundSteering		= 1.0;
		gPlayerInfo[i].groundAcceleration	= 1.0;
		gPlayerInfo[i].noSkids				= false;


				/* WEAPON INFO */

		gPlayerInfo[i].powType				= POW_TYPE_NONE;
		gPlayerInfo[i].powTypeBeingThrown	= POW_TYPE_NONE;
		gPlayerInfo[i].powQuantity			= 0;

				/* CAMERA */

		gPlayerInfo[i].cameraRingRot = 0;
		gPlayerInfo[i].cameraUserRot = 0;

		gPlayerInfo[i].camera.cameraLocation.x = 0;
		gPlayerInfo[i].camera.cameraLocation.y = 0;
		gPlayerInfo[i].camera.cameraLocation.z = 0;
		gPlayerInfo[i].camera.pointOfInterest.x = 0;
		gPlayerInfo[i].camera.pointOfInterest.y = 0;
		gPlayerInfo[i].camera.pointOfInterest.z = 0;
		gPlayerInfo[i].camera.upVector.x = 0;
		gPlayerInfo[i].camera.upVector.y = 1;
		gPlayerInfo[i].camera.upVector.z = 0;

		gPlayerInfo[i].cameraMode = CAMERA_MODE_NORMAL1;


			/* AI */

		gPlayerInfo[i].oldPositionTimer	= 0;
		gPlayerInfo[i].oldPosition.x	= 0;
		gPlayerInfo[i].oldPosition.y	= 0;
		gPlayerInfo[i].oldPosition.z	= 0;
		gPlayerInfo[i].reverseTimer		= 0;
		gPlayerInfo[i].rescueTimer		= 0;
		gPlayerInfo[i].rescueProgress	= -1;					// lap -1, checkpoint N-1 (CPURaceProgress)
		gPlayerInfo[i].rescueBestDist	= CPU_RESCUE_NO_DIST;
		gPlayerInfo[i].rescueX			= gPlayerInfo[i].startX;		// until it crosses a checkpoint: its grid slot
		gPlayerInfo[i].rescueZ			= gPlayerInfo[i].startZ;
		gPlayerInfo[i].rescueDirX		= -sinf(gPlayerInfo[i].startRotY);
		gPlayerInfo[i].rescueDirZ		= -cosf(gPlayerInfo[i].startRotY);
		gPlayerInfo[i].attackTimer		= 2;					// dont attack for the first few seconds
		gPlayerInfo[i].targetedPlayer	= -1;					// no players targeted yet
		gPlayerInfo[i].targetingTimer	= 0;
		gPlayerInfo[i].net.cpuPOWType	= POW_TYPE_NONE;		// no host-scheduled POW use (network games)
		gPlayerInfo[i].pathVec.x	= 0;
		gPlayerInfo[i].pathVec.y	= 0;



			/* SOUND */

		gPlayerInfo[i].engineChannel = -1;


			/* SCORING */

		SDL_memset(gPlayerInfo[i].lapTimes, 0, sizeof(gPlayerInfo[i].lapTimes));


			/* RESET CAR PHYSICS INFO */

		SetPhysicsForVehicleType(i);
	}


	SetDefaultCameraModeForAllPlayers();
}


/***************** SYNCED CPU VEHICLE RANDOM *********************/
//
// Local CPU vehicles on Hard come from the synced RNG.
//

static uint16_t SyncedCPUVehicleRandom(void* context, int cpuIndex, uint16_t min, uint16_t max)
{
	(void) context;
	(void) cpuIndex;
	return RandomRange(min, max);
}


/******************** GET/SET PLAYER DRIVER LOOKS ***********************/

static void GetPlayerDriverLooks(DriverLook looks[MAX_PLAYERS])
{
	for (int i = 0; i < MAX_PLAYERS; i++)
	{
		looks[i].sex	= gPlayerInfo[i].sex;
		looks[i].skin	= gPlayerInfo[i].skin;
	}
}

static void SetPlayerDriverLooks(const DriverLook looks[MAX_PLAYERS])
{
	for (int i = 0; i < MAX_PLAYERS; i++)
	{
		gPlayerInfo[i].sex	= looks[i].sex;
		gPlayerInfo[i].skin	= looks[i].skin;
	}
}


/******************** GET PLAYER OUTFIT RANK ***********************/
//
// How many lower-numbered players wear playerNum's outfit (see GetDriverOutfitRank).
//

int GetPlayerOutfitRank(short playerNum)
{
DriverLook	looks[MAX_PLAYERS];

	GAME_ASSERT(playerNum >= 0 && playerNum < MAX_PLAYERS);

	GetPlayerDriverLooks(looks);
	return GetDriverOutfitRank(looks, playerNum);
}


/******************** NUM PLAYERS TO DRESS ***********************/
//
// Character select swaps looks only among the cars in this game (InitPlayerInfo_Game has
// counted them), so a slot the race won't use never holds on to an outfit.
//

static int NumPlayersToDress(short whichPlayer)
{
	return SDL_clamp((int) gNumTotalPlayers, whichPlayer + 1, MAX_PLAYERS);
}


/******************** CYCLE PLAYER OUTFIT ***********************/
//
// Character select: steps whichPlayer to the next outfit. Players in playersDone keep their
// looks; with dressOthers, whoever wore the new look takes the old one so drivers who looked
// different still do. Returns the new outfit.
//

short CyclePlayerOutfit(short whichPlayer, int delta, uint32_t playersDone, Boolean dressOthers)
{
DriverLook	looks[MAX_PLAYERS];

	GAME_ASSERT(whichPlayer >= 0 && whichPlayer < MAX_PLAYERS);

	GetPlayerDriverLooks(looks);
	const short newSkin = CycleDriverOutfit(looks, NumPlayersToDress(whichPlayer), whichPlayer, delta, playersDone, dressOthers);
	SetPlayerDriverLooks(looks);

	return newSkin;
}


/******************** SET PLAYER BODY ***********************/
//
// Character select: the body whichPlayer chose, with the same rules as CyclePlayerOutfit.
//

void SetPlayerBody(short whichPlayer, short sex, uint32_t playersDone, Boolean dressOthers)
{
DriverLook	looks[MAX_PLAYERS];

	GAME_ASSERT(whichPlayer >= 0 && whichPlayer < MAX_PLAYERS);

	GetPlayerDriverLooks(looks);
	ChangeDriverBody(looks, NumPlayersToDress(whichPlayer), whichPlayer, sex, playersDone, dressOthers);
	SetPlayerDriverLooks(looks);
}


#pragma mark -

/********* SET PLAYER PARMS FROM TILE ATTRIBUTES *****************/
//
// INPUT: 	flags = tile attribute flags
//

void SetPlayerParmsFromTileAttributes(short playerNum, uint16_t flags)
{
			/* IF ON WATER */

	if (gPlayerInfo[playerNum].onWater)
	{
		gPlayerInfo[playerNum].groundTraction = .3;
		gPlayerInfo[playerNum].groundFriction = .3;
		gPlayerInfo[playerNum].groundSteering = .3;
		gPlayerInfo[playerNum].groundAcceleration = .5;
		gPlayerInfo[playerNum].noSkids			= true;
		gPlayerInfo[playerNum].dragDebrisTexture = PARTICLE_SObjType_Splash;
		gPlayerInfo[playerNum].alwaysDoDrag 	= true;
		return;
	}

			/* SEE IF ON ICE */

	if (flags & TILE_ATTRIB_ICE)
	{
		gPlayerInfo[playerNum].groundTraction = .05f;
		gPlayerInfo[playerNum].groundFriction = .05f;
		gPlayerInfo[playerNum].groundSteering = .2f;
		gPlayerInfo[playerNum].groundAcceleration = .3;			// original 1999 value (fork had raised this to .5, over-buffing ice accel for every car; not needed since the on-ground launch boost already unsticks low-accel cars on ice)
		gPlayerInfo[playerNum].noSkids			= true;
		gPlayerInfo[playerNum].dragDebrisTexture = -1;
		gPlayerInfo[playerNum].alwaysDoDrag 	= false;
	}

			/* SEE IF ON SNOW */

	else
	if (flags & TILE_ATTRIB_SNOW)
	{
		gPlayerInfo[playerNum].groundTraction = .3f;
		gPlayerInfo[playerNum].groundFriction = .5f;
		gPlayerInfo[playerNum].groundSteering = .6f;
		gPlayerInfo[playerNum].groundAcceleration = .7;
		gPlayerInfo[playerNum].noSkids			= false;
		gPlayerInfo[playerNum].skidColor.r		= 1.0f;
		gPlayerInfo[playerNum].skidColor.g		= 1.0f;
		gPlayerInfo[playerNum].skidColor.b		= 1.0f;
		gPlayerInfo[playerNum].dragDebrisTexture = PARTICLE_SObjType_SnowDust;
		gPlayerInfo[playerNum].alwaysDoDrag 	= true;
	}

		/* SET DEFAULTS */

	else
	{
		gPlayerInfo[playerNum].groundTraction = 1.0;
		gPlayerInfo[playerNum].groundFriction = 1.0;
		gPlayerInfo[playerNum].groundSteering = 1.0;
		gPlayerInfo[playerNum].groundAcceleration = 1.0;
		gPlayerInfo[playerNum].noSkids			= false;
		gPlayerInfo[playerNum].skidColor.r		= .0f;
		gPlayerInfo[playerNum].skidColor.g		= .0f;
		gPlayerInfo[playerNum].skidColor.b		= .0f;
		gPlayerInfo[playerNum].dragDebrisTexture = PARTICLE_SObjType_Dirt;
		gPlayerInfo[playerNum].alwaysDoDrag 	= false;
	}


}




#pragma mark -

/******************** FIND CLOSEST PLAYER ****************************/
//
// Returns -1 if no other players in range
//
// Ignore thePlayer if != nil
//

short FindClosestPlayer(ObjNode *thePlayer, float x, float z, float range, Boolean allowCPUCars, float *dist)
{
short	p,bestP = -1;
ObjNode	*target;
float	bestDist = 10000000000;
float	d;

	for (p = 0; p < gNumTotalPlayers; p++)
	{
		target = gPlayerInfo[p].objNode;

		if (target == thePlayer)
			continue;

		if (!allowCPUCars)
		{
			if (gPlayerInfo[p].isComputer)
				continue;
		}

		d = CalcDistance(x,z, target->Coord.x, target->Coord.z);
		if (d > range)
			continue;

		if (d < bestDist)
		{
			bestDist = d;
			bestP = p;
		}
	}

	*dist = bestDist;
	return(bestP);
}



/******************** FIND CLOSEST PLAYER IN FRONT ****************************/
//
// Returns -1 if no other players in range
//
// INPUT:  angle, 0 = full 180 degrees in front, 1.0 = none
//

short FindClosestPlayerInFront(ObjNode *theNode, float range, Boolean allowCPUCars, float *dist, float angle)
{
short	p,bestP = -1;
ObjNode	*target;
float	bestDist = 10000000000;
float	x,z,d, r, dot;
OGLVector2D	aimVec, toVec;

	x = theNode->Coord.x;
	z = theNode->Coord.z;

	r = theNode->Rot.y;							// calc aim vector
	aimVec.x = -sin(r);
	aimVec.y = -cos(r);


	for (p = 0; p < gNumTotalPlayers; p++)
	{
		target = gPlayerInfo[p].objNode;

		if (target == theNode)
			continue;

		if (!allowCPUCars)
		{
			if (gPlayerInfo[p].isComputer)
				continue;
		}

		d = CalcDistance(x,z, target->Coord.x, target->Coord.z);		// calc dist & check range
		if (d > range)
			continue;

		toVec.x = target->Coord.x - x;
		toVec.y = target->Coord.z - z;
		FastNormalizeVector2D(toVec.x, toVec.y, &toVec, true);			// calc normal to target

		dot = OGLVector2D_Dot(&aimVec, &toVec);							// dot = angle
		if (dot < angle)													// if in back, then skip
			continue;

		if (d < bestDist)
		{
			bestDist = d;
			bestP = p;
		}
	}

	*dist = bestDist;
	return(bestP);
}


/******************** FIND CLOSEST PLAYER IN BACK ****************************/
//
// Returns -1 if no other players in range
//
// INPUT:  angle, 0 = full 180 degrees, -1.0 = none
//

short FindClosestPlayerInBack(ObjNode *theNode, float range, Boolean allowCPUCars, float *dist, float angle)
{
short	p,bestP = -1;
ObjNode	*target;
float	bestDist = 10000000000;
float	x,z,d, r, dot;
OGLVector2D	aimVec, toVec;

	x = theNode->Coord.x;
	z = theNode->Coord.z;

	r = theNode->Rot.y;							// calc aim vector
	aimVec.x = -sin(r);
	aimVec.y = -cos(r);


	for (p = 0; p < gNumTotalPlayers; p++)
	{
		target = gPlayerInfo[p].objNode;

		if (target == theNode)
			continue;

		if (!allowCPUCars)
		{
			if (gPlayerInfo[p].isComputer)
				continue;
		}

		d = CalcDistance(x,z, target->Coord.x, target->Coord.z);		// calc dist & check range
		if (d > range)
			continue;

		toVec.x = target->Coord.x - x;
		toVec.y = target->Coord.z - z;
		FastNormalizeVector2D(toVec.x, toVec.y, &toVec, true);			// calc normal to target

		dot = OGLVector2D_Dot(&aimVec, &toVec);							// dot = angle
		if (dot > angle)												// if in front, then skip
			continue;

		if (d < bestDist)
		{
			bestDist = d;
			bestP = p;
		}
	}

	*dist = bestDist;
	return(bestP);
}


#pragma mark -


/******************* CHOOSE TAGGED PLAYER *********************/
//
// Selects the tagged ("it") player starting from startIndex, skipping eliminated players. The
// caller supplies startIndex so it can choose whether that index comes from the synced RNG (the
// aligned init/leave contexts) or a stateless deterministic sample (the in-sim re-selection, which
// must not advance gSimRNG lest a divergent trigger frame desync the seed).
//
void ChooseTaggedPlayerWithIndex(short startIndex)
{
short	i,j;
	if (gNumTotalPlayers <= 0)					// nothing to tag; avoids a negative clamp -> gPlayerInfo[-1]
		return;

	for (int p = 0; p < gNumTotalPlayers; p++)
		gPlayerInfo[p].isIt = false;

	if (startIndex < 0)							// clamp into range (stateless sample can round up to ==count)
		startIndex = 0;
	if (startIndex >= gNumTotalPlayers)
		startIndex = gNumTotalPlayers - 1;

	i = j = startIndex;

	while(gPlayerInfo[i].isEliminated)
	{
		i++;
		if (i >= gNumTotalPlayers)			// wrap around
			i = 0;
		if (i == j)							// error check
			DoFatalAlert("ChooseTaggedPlayer: all players have been eliminated");
	}

	gPlayerInfo[i].isIt = true;
	gWhoIsIt = i;

}

void ChooseTaggedPlayer(void)
{
	// Init (race start) and leave-conversion paths run at frame-aligned points, so the synced RNG
	// draw lands at the same stream position on every peer.
	ChooseTaggedPlayerWithIndex(RandomRange(0, gNumTotalPlayers-1));
}

/*********************** UPDATE TAG MARKER **********************/
//
// Occilatets the filter color on the tagged player
//

void UpdateTagMarker(void)
{
short	p;
float	o,r,g,b;
ObjNode	*obj;

	for (p = 0; p < gNumTotalPlayers; p++)					// scan all players to update their colors
	{
		if (p == gWhoIsIt)
		{
			o = gPlayerInfo[p].tagOccilation += gFramesPerSecondFrac * 2.0f;

			gTagColor.r = r = fabs(sin(o));
			gTagColor.g = g = fabs(cos(o));
			gTagColor.b = b = fabs(tan(o));

			obj = gPlayerInfo[p].objNode;
			while(obj)
			{
				obj->ColorFilter.r = r;
				obj->ColorFilter.g = g;
				obj->ColorFilter.b = b;
				obj = obj->ChainNode;
			}
		}
		else
		{
			obj = gPlayerInfo[p].objNode;
			while(obj)
			{
				obj->ColorFilter.r =
				obj->ColorFilter.g =
				obj->ColorFilter.b = 1.0;
				obj = obj->ChainNode;
			}
		}
	}
}



/***************** PLAYER LOSE HEALTH ************************/

void PlayerLoseHealth(short p, float damage)
{

	if (gTrackCompleted)
		return;

	if (gPlayerInfo[p].isEliminated)
		return;

	gPlayerInfo[p].health -= damage;

			/* SEE IF DEAD */

	if (gPlayerInfo[p].health <= 0.0f)
	{
		gPlayerInfo[p].health = 0;
		gPlayerInfo[p].isEliminated = true;
		gNumPlayersEliminated++;

		if (gNumPlayersEliminated < (gNumTotalPlayers-1))		// if more than 1 player remaining, then post ELIMINATED message
		{
			ShowWinLose(p, 0, 0);								// this player is eliminated
		}
	}
}

#pragma mark -

/******************** SET STICKY TIRES *************************/

void SetStickyTires(short playerNum)
{
	SetTractionPhysics(&gPlayerInfo[playerNum].carStats, 3.0);
	gPlayerInfo[playerNum].stickyTiresTimer = 20.0;				// set duration of sticky tires


}

/******************** SET SUSPENSION POW *************************/

void SetSuspensionPOW(short playerNum)
{
	SetSuspensionPhysics(&gPlayerInfo[playerNum].carStats, 3.0);
	gPlayerInfo[playerNum].superSuspensionTimer = 20.0;				// set duration


}


/****************** SET CAR STATUS BITS *******************/

void SetCarStatusBits(short	playerNum, uint32_t bits)
{
ObjNode *obj;

	obj = gPlayerInfo[playerNum].objNode;
	while(obj)
	{
		obj->StatusBits |= bits;
		obj = obj->ChainNode;
	}
}

/****************** CLEAR CAR STATUS BITS *******************/

void ClearCarStatusBits(short	playerNum, uint32_t bits)
{
ObjNode *obj;

	obj = gPlayerInfo[playerNum].objNode;
	while(obj)
	{
		obj->StatusBits &= ~bits;
		obj = obj->ChainNode;
	}
}






