/****************************/
/*   		ITEMS.C		    */
/* (c)2000 Pangea Software  */
/* By Brian Greenstone      */
/****************************/


/****************************/
/*    EXTERNALS             */
/****************************/


#include "globals.h"
#include "misc.h"
#include "objects.h"
#include "items.h"
#include "mobjtypes.h"
#include "collision.h"
#include "3dmath.h"
#include "sound2.h"
#include "terrain.h"
#include "effects.h"
#include "skeletonobj.h"
#include "triggers.h"
#include "main.h"
#include "bg3d.h"
#include "player.h"
#include "sobjtypes.h"
#include "splineitems.h"

extern	float				gFramesPerSecondFrac,gFramesPerSecond;
extern	OGLPoint3D			gCoord;
extern	OGLVector3D			gDelta;
extern	NewObjectDefinitionType	gNewObjectDefinition;
extern	OGLBoundingBox 		gObjectGroupBBoxList[MAX_BG3D_GROUPS][MAX_OBJECTS_IN_GROUP];
extern	OGLSetupOutputType	*gGameViewInfoPtr;
extern	uint32_t				gAutoFadeStatusBits,gInfobarUpdateBits;
extern	Boolean				gAreaCompleted;
extern	PlayerInfoType		gPlayerInfo[];
extern	const float gWaterHeights[NUM_TRACKS][6];
extern	int					gTrackNum;
extern	SplineDefType	**gSplineList;
extern	Boolean gAnnouncedPOW[MAX_POW_TYPES];
extern	short				gNumTorches;

/****************************/
/*    PROTOTYPES            */
/****************************/

static void MoveTorchPot(ObjNode *theNode);
static void MoveAtlantisStartline(ObjNode *theNode);
static void MoveBubbleGenerator(ObjNode *theNode);
static void MoveVolcano(ObjNode *theNode);
static void MovePolarBear(ObjNode *theNode);
static void MoveViking(ObjNode *theNode);


/****************************/
/*    CONSTANTS             */
/****************************/

#define	HENGE_PYLON_RADIUS	900.0f

#define	BEAR_SCALE			3.0f
#define	VIKING_SCALE		1.6f

/*********************/
/*    VARIABLES      */
/*********************/

ObjNode	*gCycloramaObj = nil;




/********************* INIT ITEMS MANAGER *************************/

void InitItemsManager(void)
{
int	i;

	gCycloramaObj = nil;

	CreateCyclorama();

	for (i = 0; i < MAX_POW_TYPES; i++)
		gAnnouncedPOW[i] = false;

	gNumTorches = 0;
}


/************************* CREATE CYCLORAMA *********************************/
//
// This one cyc is used for all players on this machine.  Coords are reset during player render loop for each player.
//

void CreateCyclorama(void)
{
	gNewObjectDefinition.group	= MODEL_GROUP_LEVELSPECIFIC;
	gNewObjectDefinition.type 	= 0;						// cyc is always 1st model in level bg3d files
	gNewObjectDefinition.coord.x = 0;
	gNewObjectDefinition.coord.y = 0;
	gNewObjectDefinition.coord.z = 0;
	gNewObjectDefinition.flags 	= STATUS_BIT_DONTCULL|STATUS_BIT_NOLIGHTING|STATUS_BIT_NOFOG;
	gNewObjectDefinition.slot 	= 0;
	gNewObjectDefinition.moveCall = nil;
	gNewObjectDefinition.rot 	= 0;
	gNewObjectDefinition.scale 	= gGameViewInfoPtr->yon * .99f;
	gCycloramaObj = MakeNewDisplayGroupObject(&gNewObjectDefinition);
	if (gCycloramaObj == nil)
		return;

}



#pragma mark -






/************************* ADD FINISH LINE *********************************/

Boolean AddFinishLine(TerrainItemEntryType *itemPtr, long  x, long z)
{
ObjNode	*newObj;
CollisionBoxType *boxPtr;
float			y,d;
OGLMatrix3x3	m;
OGLPoint2D		p,p1,p2;

static const float xOff[] =
{
	2490,							// desert
	0,								// jungle
	1825,							// ice

	0,								// crete
	1806,							// china
	1751,							// egypt

	1498,							// europe
	1920,							// viking
	0,								// atlantis

};

static const float diameter[] =
{
	422,							// desert
	0,								// jungle
	255,							// ice

	0,								// crete
	354,							// china
	220,							// egypt

	423,							// europe
	280,							// viking
	0,								// atlantis
};


	gNewObjectDefinition.group 		= MODEL_GROUP_LEVELSPECIFIC;
	gNewObjectDefinition.type 		= DESERT_ObjType_StartingLine;
	gNewObjectDefinition.coord.x 	= x;
	gNewObjectDefinition.coord.z 	= z;
	gNewObjectDefinition.coord.y 	= y = GetMinTerrainY(x,z, gNewObjectDefinition.group, gNewObjectDefinition.type, 1.0);
	gNewObjectDefinition.flags 		= STATUS_BIT_NOLIGHTING|gAutoFadeStatusBits;
	gNewObjectDefinition.slot 		= 100;
	gNewObjectDefinition.moveCall 	= MoveStaticObject;
	gNewObjectDefinition.rot 		= PI2 * ((float)itemPtr->parm[0] * (1.0f/8.0f));
	gNewObjectDefinition.scale 	    = 1;
	newObj = MakeNewDisplayGroupObject(&gNewObjectDefinition);
	if (newObj == nil)
		return(false);

	newObj->TerrainItemPtr = itemPtr;								// keep ptr to item list


	switch(gTrackNum)
	{
				/*********/
				/* CRETE */
				/*********/

		case	TRACK_NUM_CRETE:

				newObj->CType 			= CTYPE_MISC;
				newObj->CBits			= CBITS_ALLSOLID;

						/* BUILD 4 COLLISION BOXES */

				AllocateCollisionBoxMemory(newObj, 4);								// alloc 4 collision boxes
				boxPtr = newObj->CollisionBoxes;										// get ptr to boxes
				boxPtr[0].left 		= x - 4030.0f;
				boxPtr[0].right 	= x - 3517.0f;
				boxPtr[0].top 		= y + 2000.0f;
				boxPtr[0].bottom 	= y - 10.0f;
				boxPtr[0].back 		= z - 1000.0f;
				boxPtr[0].front 	= z + 1000.0f;

				boxPtr[1].left 		= x - 1674.0f;
				boxPtr[1].right 	= x - 1233.0f;
				boxPtr[1].top 		= y + 2000.0f;
				boxPtr[1].bottom 	= y - 10.0f;
				boxPtr[1].back 		= z - 1000.0f;
				boxPtr[1].front 	= z + 1000.0f;

				boxPtr[2].left 		= x + 1233.0f;
				boxPtr[2].right 	= x + 1674.0f;
				boxPtr[2].top 		= y + 2000.0f;
				boxPtr[2].bottom 	= y - 10.0f;
				boxPtr[2].back 		= z - 1000.0f;
				boxPtr[2].front 	= z + 1000.0f;

				boxPtr[3].left 		= x + 3517.0f;
				boxPtr[3].right 	= x + 4030.0f;
				boxPtr[3].top 		= y + 2000.0f;
				boxPtr[3].bottom 	= y - 10.0f;
				boxPtr[3].back 		= z - 1000.0f;
				boxPtr[3].front 	= z + 1000.0f;

				KeepOldCollisionBoxes(newObj);


				break;

				/************/
				/* ATLANTIS */
				/************/

		case	TRACK_NUM_ATLANTIS:
				newObj->MoveCall = MoveAtlantisStartline;
				break;


				/************/
				/* JUNGLE   */
				/************/

		case	TRACK_NUM_JUNGLE:
				newObj->CType 			= CTYPE_MISC | CTYPE_AVOID;
				newObj->CBits			= CBITS_ALLSOLID;
				CreateCollisionBoxFromBoundingBox(newObj, 1.0, 1.0);
				break;



				/********************/
				/* BRIDGE COLLISION */
				/********************/

		default:

						/* SET COLLISION STUFF */

				newObj->CType 			= CTYPE_MISC;
				newObj->CBits			= CBITS_ALLSOLID;


					/* CALC COORDS OF EACH SUPPORT BEAM */

				OGLMatrix3x3_SetRotate(&m, -newObj->Rot.y);
				p.x = -xOff[gTrackNum];
				p.y = 0;
				OGLPoint2D_Transform(&p, &m, &p1);				// calc left
				p.x = -p.x;
				OGLPoint2D_Transform(&p, &m, &p2);				// calc right

						/* BUILD 2 COLLISION BOXES */

				d = diameter[gTrackNum];

				AllocateCollisionBoxMemory(newObj, 2);					// alloc 2 collision boxes
				boxPtr = newObj->CollisionBoxes;						// get ptr to boxes
				boxPtr[0].left 		= newObj->Coord.x + p1.x - d;
				boxPtr[0].right 	= newObj->Coord.x + p1.x + d;
				boxPtr[0].top 		= newObj->Coord.y + 1000.0;
				boxPtr[0].bottom 	= newObj->Coord.y - 10.0f;
				boxPtr[0].back 		= newObj->Coord.z + p1.y - d;
				boxPtr[0].front 	= newObj->Coord.z + p1.y + d;
				boxPtr[1].left 		= newObj->Coord.x + p2.x - d;
				boxPtr[1].right 	= newObj->Coord.x + p2.x + d;
				boxPtr[1].top 		= newObj->Coord.y + 1000.0;
				boxPtr[1].bottom 	= newObj->Coord.y - 10.0f;
				boxPtr[1].back 		= newObj->Coord.z + p2.y - d;
				boxPtr[1].front 	= newObj->Coord.z + p2.y + d;
				KeepOldCollisionBoxes(newObj);

	}







	return(true);													// item was added
}

/********** MOVE ATLANTIS START LINE ********************/

static void MoveAtlantisStartline(ObjNode *theNode)
{
	if (TrackTerrainItem(theNode))							// just check to see if it's gone
	{
		DeleteObject(theNode);
		return;
	}

	theNode->SpecialF[0] += gFramesPerSecondFrac * 3.0f;

	GetObjectInfo(theNode);

	gCoord.y = theNode->InitCoord.y + 1000.0f + sin(theNode->SpecialF[0]) * 300.0f;

	UpdateObject(theNode);

}


/************************* ADD TREE *********************************/

Boolean AddTree(TerrainItemEntryType *itemPtr, long  x, long z)
{
ObjNode	*newObj;
static const short types[NUM_TRACKS][4] =
{
	[TRACK_NUM_DESERT]      = {0,0,0,0},
	[TRACK_NUM_JUNGLE]      = {JUNGLE_ObjType_Tree1,JUNGLE_ObjType_Tree2,JUNGLE_ObjType_Tree3,JUNGLE_ObjType_Tree3},
	[TRACK_NUM_ICE]         = {ICE_ObjType_Tree,0,0,0},
	[TRACK_NUM_CRETE]       = {CRETE_ObjType_TallTree,CRETE_ObjType_WideTree,0,0},
	[TRACK_NUM_CHINA]       = {0,0,0,0},
	[TRACK_NUM_EGYPT]       = {0,0,0,0},
	[TRACK_NUM_EUROPE]      = {EUROPE_ObjType_TallPine,EUROPE_ObjType_WidePine,0,0},
	[TRACK_NUM_SCANDINAVIA] = {SCANDINAVIA_ObjType_TallPine,SCANDINAVIA_ObjType_WidePine,0,0},
	[TRACK_NUM_ATLANTIS]    = {0,0,0,0},
	[TRACK_NUM_STONEHENGE]  = {0,0,0,0},
	[TRACK_NUM_AZTEC]       = {AZTEC_ObjType_Tree,0,0,0},
	[TRACK_NUM_COLISEUM]    = {0,0,0,0},
};

static const short aimAtPlayer[NUM_TRACKS][4] =
{
	[TRACK_NUM_DESERT]      = {0,0,0,0},
	[TRACK_NUM_JUNGLE]      = {false,false,false,true},
	[TRACK_NUM_ICE]         = {false,0,0,0},
	[TRACK_NUM_CRETE]       = {false,false,0,0},
	[TRACK_NUM_CHINA]       = {0,0,0,0},
	[TRACK_NUM_EGYPT]       = {0,0,0,0},
	[TRACK_NUM_EUROPE]      = {0,0,0,0},
	[TRACK_NUM_SCANDINAVIA] = {0,0,0,0},
	[TRACK_NUM_ATLANTIS]    = {0,0,0,0},
	[TRACK_NUM_STONEHENGE]  = {0,0,0,0},
	[TRACK_NUM_AZTEC]       = {false,0,0,0},
	[TRACK_NUM_COLISEUM]    = {0,0,0,0},
};

Boolean	isSolid = itemPtr->parm[3] & 1;

	gNewObjectDefinition.group 		= MODEL_GROUP_LEVELSPECIFIC;
	gNewObjectDefinition.type 		= types[gTrackNum][itemPtr->parm[0]];
	gNewObjectDefinition.coord.x 	= x;
	gNewObjectDefinition.coord.z 	= z;
	gNewObjectDefinition.coord.y 	= GetTerrainY(x,z);

	if (itemPtr->parm[3] & (1<<1))											// see if bump up
		gNewObjectDefinition.coord.y += 500.0f;

	gNewObjectDefinition.flags 		= gAutoFadeStatusBits|STATUS_BIT_KEEPBACKFACES|STATUS_BIT_NOLIGHTING|STATUS_BIT_NOTEXTUREWRAP|STATUS_BIT_CLIPALPHA;
	if (aimAtPlayer[gTrackNum][itemPtr->parm[0]])
	{
		gNewObjectDefinition.flags |= STATUS_BIT_AIMATCAMERA;
	}

	if (isSolid)
		gNewObjectDefinition.slot 		= 642;
	else
		gNewObjectDefinition.slot 		= SLOT_OF_DUMB;

	gNewObjectDefinition.moveCall 	= MoveStaticObject;
	gNewObjectDefinition.rot 		= 0;
	gNewObjectDefinition.scale 		= 1.0 + RandomFloat() * .3f;
	newObj = MakeNewDisplayGroupObject(&gNewObjectDefinition);
	if (newObj == nil)
		return(false);

	newObj->TerrainItemPtr = itemPtr;								// keep ptr to item list

	if (isSolid)
	{
				/* SET COLLISION STUFF */

		newObj->CType 			= CTYPE_MISC|CTYPE_AVOID;
		newObj->CBits			= CBITS_ALLSOLID;
		SetObjectCollisionBounds(newObj, 1000, -10, -50, 50, 50, -50);
	}


	return(true);													// item was added
}


/************************* ADD VINE *********************************/

Boolean AddVine(TerrainItemEntryType *itemPtr, long  x, long z)
{
ObjNode	*newObj;


	gNewObjectDefinition.group 		= MODEL_GROUP_LEVELSPECIFIC;
	gNewObjectDefinition.type 		= JUNGLE_ObjType_Vine;
	gNewObjectDefinition.coord.x 	= x;
	gNewObjectDefinition.coord.z 	= z;
	gNewObjectDefinition.coord.y 	= GetTerrainY(x,z);
	gNewObjectDefinition.flags 		= gAutoFadeStatusBits|STATUS_BIT_NOLIGHTING|STATUS_BIT_CLIPALPHA;
	gNewObjectDefinition.slot 		= SLOT_OF_DUMB+1;
	gNewObjectDefinition.moveCall 	= MoveStaticObject;
	gNewObjectDefinition.rot 		= PI2 * ((float)itemPtr->parm[0] * (1.0f/8.0f));
	gNewObjectDefinition.scale 		= 1.0;
	newObj = MakeNewDisplayGroupObject(&gNewObjectDefinition);
	if (newObj == nil)
		return(false);

	newObj->TerrainItemPtr = itemPtr;								// keep ptr to item list

	return(true);													// item was added
}



/************************* ADD EASTER ISLAND HEAD *********************************/

Boolean AddEasterHead(TerrainItemEntryType *itemPtr, long  x, long z)
{
ObjNode	*newObj;


	gNewObjectDefinition.group 		= MODEL_GROUP_LEVELSPECIFIC;
	gNewObjectDefinition.type 		= JUNGLE_ObjType_EasterHead;
	gNewObjectDefinition.coord.x 	= x;
	gNewObjectDefinition.coord.z 	= z;
	gNewObjectDefinition.coord.y 	= GetMinTerrainY(x,z, gNewObjectDefinition.group, gNewObjectDefinition.type, 1.0);
	gNewObjectDefinition.flags 		= gAutoFadeStatusBits;
	gNewObjectDefinition.slot 		= 10;
	gNewObjectDefinition.moveCall 	= MoveStaticObject;
	gNewObjectDefinition.rot 		= (float)itemPtr->parm[0] / 8.0f * PI2;
	gNewObjectDefinition.scale 		= 1.0;
	newObj = MakeNewDisplayGroupObject(&gNewObjectDefinition);
	if (newObj == nil)
		return(false);

	newObj->TerrainItemPtr = itemPtr;								// keep ptr to item list

			/* SET COLLISION STUFF */

	newObj->CType 			= CTYPE_MISC|CTYPE_AVOID;
	newObj->CBits			= CBITS_ALLSOLID;
	CreateCollisionBoxFromBoundingBox(newObj,.9,1);

	return(true);													// item was added
}

#pragma mark -


/************************* ADD PILLAR *********************************/

typedef struct
{
	short 	type[4];
	float	tweakXZ[4],tweakY[4];
}ColumnInfo;

Boolean AddPillar(TerrainItemEntryType *itemPtr, long  x, long z)
{
ObjNode	*newObj;
static const ColumnInfo info[NUM_TRACKS] =
{
	[TRACK_NUM_DESERT] =
	{
		{DESERT_ObjType_RockColumn1,DESERT_ObjType_RockColumn2,DESERT_ObjType_RockColumn3,0},
		{1,1,.9,1},
		{1,1,1,1},
	},

	[TRACK_NUM_JUNGLE] =
	{
		{0,0,0,0},
		{1,1,1,1},
		{1,1,1,1},
	},

	[TRACK_NUM_ICE] =
	{
		{0,0,0,0},
		{1,1,1,1},
		{1,1,1,1},
	},

	[TRACK_NUM_CRETE] =
	{
		{CRETE_ObjType_Column1,CRETE_ObjType_Column2,CRETE_ObjType_Column3,CRETE_ObjType_Column1},
		{1,1,.8,1},
		{1,1,1,1},
	},

	[TRACK_NUM_CHINA] =
	{
		{0,0,0,0},
		{1,1,1,1},
		{1,1,1,1},
	},

	[TRACK_NUM_EGYPT] =
	{
		{EGYPT_ObjType_Pillar,EGYPT_ObjType_Obelisk,0,0},
		{.9,1,1,1},
		{1,1,1,1},
	},

	[TRACK_NUM_EUROPE] =
	{
		{0,0,0,0},
		{1,1,1,1},
		{1,1,1,1},
	},

	[TRACK_NUM_SCANDINAVIA] =
	{
		{SCANDINAVIA_ObjType_LookoutTower,0,0,0},
		{1,1,1,1},
		{1,1,1,1},
	},

	[TRACK_NUM_ATLANTIS] =
	{
		{ATLANTIS_ObjType_Tower,ATLANTIS_ObjType_Column1,ATLANTIS_ObjType_Column2,0},
		{1,.6,.6,1},
		{.5,1,1,1},
	},

	[TRACK_NUM_STONEHENGE] =
	{
		{0,0,0,0},
		{1,1,1,1},
		{1,1,1,1},
	},

	[TRACK_NUM_AZTEC] =
	{
		{0,0,0,0},
		{1,1,1,1},
		{1,1,1,1},
	},

	[TRACK_NUM_COLISEUM] =
	{
		{COLISEUM_ObjType_Column,0,0,0},
		{.9,1,1,1},
		{1,1,1,1},
	},
};

Boolean	notSolid = itemPtr->parm[3] & 1;						// see if solid or not
short	type = itemPtr->parm[0];

	gNewObjectDefinition.group 		= MODEL_GROUP_LEVELSPECIFIC;
	gNewObjectDefinition.type 		= info[gTrackNum].type[type];
	gNewObjectDefinition.coord.x 	= x;
	gNewObjectDefinition.coord.z 	= z;
	gNewObjectDefinition.coord.y 	= GetMinTerrainY(x,z, gNewObjectDefinition.group, gNewObjectDefinition.type, 1.0);
	gNewObjectDefinition.flags 		= gAutoFadeStatusBits|STATUS_BIT_CLIPALPHA;

	if (notSolid)
		gNewObjectDefinition.slot 		= SLOT_OF_DUMB+2;
	else
		gNewObjectDefinition.slot 		= 90;

	gNewObjectDefinition.moveCall 	= MoveStaticObject;
	gNewObjectDefinition.rot 		= 0;
	gNewObjectDefinition.scale 		= 1.0;
	newObj = MakeNewDisplayGroupObject(&gNewObjectDefinition);
	if (newObj == nil)
		return(false);

	newObj->TerrainItemPtr = itemPtr;								// keep ptr to item list

			/* SET COLLISION STUFF */

	if (!notSolid)
	{
		newObj->CType 			= CTYPE_MISC|CTYPE_AVOID;
		newObj->CBits			= CBITS_ALLSOLID;

		CreateCollisionBoxFromBoundingBox(newObj,info[gTrackNum].tweakXZ[type],info[gTrackNum].tweakY[type]);

	}

	return(true);													// item was added
}

/************************* ADD PYLON *********************************/

Boolean AddPylon(TerrainItemEntryType *itemPtr, long  x, long z)
{
ObjNode	*newObj;


	gNewObjectDefinition.group 		= MODEL_GROUP_LEVELSPECIFIC;
	gNewObjectDefinition.type 		= EGYPT_ObjType_Pylon;
	gNewObjectDefinition.coord.x 	= x;
	gNewObjectDefinition.coord.z 	= z;
	gNewObjectDefinition.coord.y 	= GetMinTerrainY(x,z, gNewObjectDefinition.group, gNewObjectDefinition.type, 1.0);
	gNewObjectDefinition.flags 		= gAutoFadeStatusBits;
	gNewObjectDefinition.slot 		= 40;
	gNewObjectDefinition.moveCall 	= MoveStaticObject;
	gNewObjectDefinition.rot 		= 0;
	gNewObjectDefinition.scale 		= 1.0;
	newObj = MakeNewDisplayGroupObject(&gNewObjectDefinition);
	if (newObj == nil)
		return(false);

	newObj->TerrainItemPtr = itemPtr;								// keep ptr to item list

			/* SET COLLISION STUFF */

	newObj->CType 			= CTYPE_MISC|CTYPE_AVOID;
	newObj->CBits			= CBITS_ALLSOLID;
	CreateCollisionBoxFromBoundingBox_Rotated(newObj,1,1);

	return(true);													// item was added
}


/************************* ADD BOAT *********************************/

Boolean AddBoat(TerrainItemEntryType *itemPtr, long  x, long z)
{
ObjNode	*newObj;
float	y;
Boolean	collision = true;



	gNewObjectDefinition.group 		= MODEL_GROUP_LEVELSPECIFIC;

	switch(gTrackNum)
	{
		case	TRACK_NUM_EGYPT:
				gNewObjectDefinition.type 		= EGYPT_ObjType_Boat;
				y = gWaterHeights[gTrackNum][itemPtr->parm[0]];	// put on top of water
				break;

		case	TRACK_NUM_CRETE:
				gNewObjectDefinition.type 		= CRETE_ObjType_Boat;
				y = gWaterHeights[gTrackNum][itemPtr->parm[0]];	// put on top of water
				break;

		case	TRACK_NUM_SCANDINAVIA:
				gNewObjectDefinition.type 		= SCANDINAVIA_ObjType_VikingShip;
				y = GetTerrainY(x,z);
				collision= false;
				break;

		case	TRACK_NUM_ATLANTIS:
				gNewObjectDefinition.type 		= ATLANTIS_ObjType_Shipwreck;
				y = GetTerrainY(x,z);
				break;

		default:
				DoFatalAlert("Can't AddBoat in track %d!", gTrackNum);
	}

	gNewObjectDefinition.coord.x 	= x;
	gNewObjectDefinition.coord.z 	= z;
	gNewObjectDefinition.coord.y 	= y;
	gNewObjectDefinition.flags 		= gAutoFadeStatusBits;
	if (collision)
		gNewObjectDefinition.slot 		= 40;
	else
		gNewObjectDefinition.slot 		= SLOT_OF_DUMB + 11;

	gNewObjectDefinition.moveCall 	= MoveStaticObject;
	gNewObjectDefinition.rot 		= PI2 * ((float)itemPtr->parm[1] * (1.0f/8.0f));
	gNewObjectDefinition.scale 		= 1.0;
	newObj = MakeNewDisplayGroupObject(&gNewObjectDefinition);
	if (newObj == nil)
		return(false);

	newObj->TerrainItemPtr = itemPtr;								// keep ptr to item list

			/* SET COLLISION STUFF */

	if (collision)
	{
		newObj->CType 			= CTYPE_MISC;
		newObj->CBits			= CBITS_ALLSOLID;
		CreateCollisionBoxFromBoundingBox_Rotated(newObj,1,1);
	}


	if (gTrackNum == TRACK_NUM_ATLANTIS)			// blue tint for atlantis
	{
		newObj->ColorFilter.r = .7;
		newObj->ColorFilter.g = .8;
		newObj->ColorFilter.b = 1.0;

	}

	return(true);													// item was added
}



/************************* ADD STATUE *********************************/
//
// p0 = rot 0..7 ccw
//

Boolean AddStatue(TerrainItemEntryType *itemPtr, long  x, long z)
{
ObjNode	*newObj;
static const short types[NUM_TRACKS][2] =
{
	[TRACK_NUM_DESERT]      = {0,0},
	[TRACK_NUM_JUNGLE]      = {0,0},
	[TRACK_NUM_ICE]         = {0,0},
	[TRACK_NUM_CRETE]       = {CRETE_ObjType_BullStatue,0},
	[TRACK_NUM_CHINA]       = {0,0},
	[TRACK_NUM_EGYPT]       = {EGYPT_ObjType_Statue, EGYPT_ObjType_CatStatue},
	[TRACK_NUM_EUROPE]      = {0,0},
	[TRACK_NUM_SCANDINAVIA] = {0,0},
	[TRACK_NUM_ATLANTIS]    = {0,0},
};


	gNewObjectDefinition.group 		= MODEL_GROUP_LEVELSPECIFIC;
	gNewObjectDefinition.type 		= types[gTrackNum][itemPtr->parm[0]];
	gNewObjectDefinition.coord.x 	= x;
	gNewObjectDefinition.coord.z 	= z;
	gNewObjectDefinition.coord.y 	= GetMinTerrainY(x,z, gNewObjectDefinition.group, gNewObjectDefinition.type, 1.0);
	gNewObjectDefinition.flags 		= gAutoFadeStatusBits;
	gNewObjectDefinition.slot 		= 42;
	gNewObjectDefinition.moveCall 	= MoveStaticObject;
	gNewObjectDefinition.rot 		= itemPtr->parm[1] * (PI/4);
	gNewObjectDefinition.scale 		= 1.0;
	newObj = MakeNewDisplayGroupObject(&gNewObjectDefinition);
	if (newObj == nil)
		return(false);

	newObj->TerrainItemPtr = itemPtr;								// keep ptr to item list

			/* SET COLLISION STUFF */

	newObj->CType 			= CTYPE_MISC|CTYPE_AVOID;
	newObj->CBits			= CBITS_ALLSOLID;

	if (gTrackNum == TRACK_NUM_CRETE)
		CreateCollisionBoxFromBoundingBox_Rotated(newObj, 1, 1);
	else
		CreateCollisionBoxFromBoundingBox(newObj,1,1);

	return(true);													// item was added
}


/************************* ADD SPHINX *********************************/

Boolean AddSphinx(TerrainItemEntryType *itemPtr, long  x, long z)
{
ObjNode	*newObj;


	gNewObjectDefinition.group 		= MODEL_GROUP_LEVELSPECIFIC;
	gNewObjectDefinition.type 		= EGYPT_ObjType_Sphinx;
	gNewObjectDefinition.coord.x 	= x;
	gNewObjectDefinition.coord.z 	= z;
	gNewObjectDefinition.coord.y 	= GetTerrainY(x,z) - 100.0f;
	gNewObjectDefinition.flags 		= gAutoFadeStatusBits;
	gNewObjectDefinition.slot 		= 50;
	gNewObjectDefinition.moveCall 	= MoveStaticObject;
	gNewObjectDefinition.rot 		= itemPtr->parm[0] * (PI/2);
	gNewObjectDefinition.scale 		= 1.0;
	newObj = MakeNewDisplayGroupObject(&gNewObjectDefinition);
	if (newObj == nil)
		return(false);

	newObj->TerrainItemPtr = itemPtr;								// keep ptr to item list

			/* SET COLLISION STUFF */

	newObj->CType 			= CTYPE_MISC;
	newObj->CBits			= CBITS_ALLSOLID;
	CreateCollisionBoxFromBoundingBox(newObj,1,1);

	return(true);													// item was added
}



#pragma mark -


/************************* ADD SIGN *********************************/

Boolean AddSign(TerrainItemEntryType *itemPtr, long  x, long z)
{
ObjNode	*newObj;


	gNewObjectDefinition.group 		= MODEL_GROUP_GLOBAL;
	gNewObjectDefinition.type 		= GLOBAL_ObjType_Sign_Fire + itemPtr->parm[0];
	gNewObjectDefinition.coord.x 	= x;
	gNewObjectDefinition.coord.z 	= z;
	gNewObjectDefinition.coord.y 	= GetTerrainY(x,z);
	gNewObjectDefinition.flags 		= gAutoFadeStatusBits|STATUS_BIT_KEEPBACKFACES|STATUS_BIT_NOTEXTUREWRAP|STATUS_BIT_CLIPALPHA;
	gNewObjectDefinition.slot 		= 10;
	gNewObjectDefinition.moveCall 	= MoveStaticObject;
	gNewObjectDefinition.rot 		= PI2 * ((float)itemPtr->parm[1] * (1.0f/8.0f));
	gNewObjectDefinition.scale 		= 1.0;
	newObj = MakeNewDisplayGroupObject(&gNewObjectDefinition);
	if (newObj == nil)
		return(false);

	newObj->TerrainItemPtr = itemPtr;								// keep ptr to item list

			/* SET COLLISION STUFF */

	newObj->CType 			= CTYPE_MISC|CTYPE_AVOID;
	newObj->CBits			= CBITS_ALLSOLID;
	SetObjectCollisionBounds(newObj, 400, -10, -50, 50, 50, -50);

	return(true);													// item was added
}





#pragma mark -


/************************* ADD STUMP *********************************/

Boolean AddStump(TerrainItemEntryType *itemPtr, long  x, long z)
{
ObjNode	*newObj;


	gNewObjectDefinition.group 		= MODEL_GROUP_LEVELSPECIFIC;
	gNewObjectDefinition.type 		= SCANDINAVIA_ObjType_Stump1 + (MyRandomLong() & 0x3);
	gNewObjectDefinition.coord.x 	= x;
	gNewObjectDefinition.coord.z 	= z;
	gNewObjectDefinition.coord.y 	= GetMinTerrainY(x,z, gNewObjectDefinition.group, gNewObjectDefinition.type, 1.0);
	gNewObjectDefinition.flags 		= gAutoFadeStatusBits;
	gNewObjectDefinition.slot 		= 400;
	gNewObjectDefinition.moveCall 	= MoveStaticObject;
	gNewObjectDefinition.rot 		= RandomFloat() * PI2;
	gNewObjectDefinition.scale 		= 1.0;
	newObj = MakeNewDisplayGroupObject(&gNewObjectDefinition);
	if (newObj == nil)
		return(false);

	newObj->TerrainItemPtr = itemPtr;								// keep ptr to item list

			/* SET COLLISION STUFF */

	newObj->CType 			= CTYPE_MISC|CTYPE_AVOID;
	newObj->CBits			= CBITS_ALLSOLID;
	CreateCollisionBoxFromBoundingBox(newObj,1,1);

	return(true);													// item was added
}

/************************* ADD VIKING FLAG *********************************/

Boolean AddVikingFlag(TerrainItemEntryType *itemPtr, long  x, long z)
{
ObjNode	*newObj;


	gNewObjectDefinition.group 		= MODEL_GROUP_LEVELSPECIFIC;
	gNewObjectDefinition.type 		= SCANDINAVIA_ObjType_VikingFlag;
	gNewObjectDefinition.coord.x 	= x;
	gNewObjectDefinition.coord.z 	= z;
	gNewObjectDefinition.coord.y 	= GetTerrainY(x,z);
	gNewObjectDefinition.flags 		= gAutoFadeStatusBits;
	gNewObjectDefinition.slot 		= 659;
	gNewObjectDefinition.moveCall 	= MoveStaticObject;
	gNewObjectDefinition.rot 		= PI2 * ((float)itemPtr->parm[0] * (1.0f/8.0f));
	gNewObjectDefinition.scale 		= 1.0;
	newObj = MakeNewDisplayGroupObject(&gNewObjectDefinition);
	if (newObj == nil)
		return(false);

	newObj->TerrainItemPtr = itemPtr;								// keep ptr to item list

			/* SET COLLISION STUFF */

	newObj->CType 			= CTYPE_MISC|CTYPE_AVOID;
	newObj->CBits			= CBITS_ALLSOLID;
	SetObjectCollisionBounds(newObj, 3000, -10, -100, 100, 100, -100);

	return(true);													// item was added
}


/************************* ADD WEAPONS RACK *********************************/

Boolean AddWeaponsRack(TerrainItemEntryType *itemPtr, long  x, long z)
{
ObjNode	*newObj;


	gNewObjectDefinition.group 		= MODEL_GROUP_LEVELSPECIFIC;
	gNewObjectDefinition.type 		= SCANDINAVIA_ObjType_WeaponsRack;
	gNewObjectDefinition.coord.x 	= x;
	gNewObjectDefinition.coord.z 	= z;
	gNewObjectDefinition.coord.y 	= GetMinTerrainY(x,z, gNewObjectDefinition.group, gNewObjectDefinition.type, 1.0);
	gNewObjectDefinition.flags 		= gAutoFadeStatusBits;
	gNewObjectDefinition.slot 		= 400;
	gNewObjectDefinition.moveCall 	= MoveStaticObject;
	gNewObjectDefinition.rot 		= PI2 * ((float)itemPtr->parm[0] * (1.0f/4.0f));
	gNewObjectDefinition.scale 		= 1.0;
	newObj = MakeNewDisplayGroupObject(&gNewObjectDefinition);
	if (newObj == nil)
		return(false);

	newObj->TerrainItemPtr = itemPtr;								// keep ptr to item list

			/* SET COLLISION STUFF */

	newObj->CType 			= CTYPE_MISC|CTYPE_AVOID;
	newObj->CBits			= CBITS_ALLSOLID;
	CreateCollisionBoxFromBoundingBox_Rotated(newObj,1,1);

	return(true);													// item was added
}


/************************* ADD BARACADE *********************************/

Boolean AddBaracade(TerrainItemEntryType *itemPtr, long  x, long z)
{
ObjNode	*newObj;


	gNewObjectDefinition.group 		= MODEL_GROUP_LEVELSPECIFIC;
	gNewObjectDefinition.type 		= SCANDINAVIA_ObjType_Baracade1 + itemPtr->parm[0];
	gNewObjectDefinition.coord.x 	= x;
	gNewObjectDefinition.coord.z 	= z;
	gNewObjectDefinition.coord.y 	= GetMinTerrainY(x,z, gNewObjectDefinition.group, gNewObjectDefinition.type, 1.0);
	gNewObjectDefinition.flags 		= gAutoFadeStatusBits;
	gNewObjectDefinition.slot 		= 300;
	gNewObjectDefinition.moveCall 	= MoveStaticObject;
	gNewObjectDefinition.rot 		= PI2 * ((float)itemPtr->parm[1] * (1.0f/4.0f));
	gNewObjectDefinition.scale 		= 1.0;
	newObj = MakeNewDisplayGroupObject(&gNewObjectDefinition);
	if (newObj == nil)
		return(false);

	newObj->TerrainItemPtr = itemPtr;								// keep ptr to item list

			/* SET COLLISION STUFF */

	newObj->CType 			= CTYPE_MISC|CTYPE_AVOID;
	newObj->CBits			= CBITS_ALLSOLID;
	CreateCollisionBoxFromBoundingBox_Rotated(newObj,1,1);

	return(true);													// item was added
}



/************************* ADD ROCK *********************************/

Boolean AddRock(TerrainItemEntryType *itemPtr, long  x, long z)
{
ObjNode	*newObj;


	gNewObjectDefinition.group 		= MODEL_GROUP_GLOBAL;
	gNewObjectDefinition.type 		= GLOBAL_ObjType_GreyRock + itemPtr->parm[0];
	gNewObjectDefinition.coord.x 	= x;
	gNewObjectDefinition.coord.z 	= z;
	gNewObjectDefinition.coord.y 	= GetTerrainY(x,z) + 30.0f;
	gNewObjectDefinition.flags 		= gAutoFadeStatusBits;
	gNewObjectDefinition.slot 		= 10;
	gNewObjectDefinition.moveCall 	= MoveStaticObject;
	gNewObjectDefinition.rot 		= RandomFloat() * PI2;
	gNewObjectDefinition.scale 		= 1.0;
	newObj = MakeNewDisplayGroupObject(&gNewObjectDefinition);
	if (newObj == nil)
		return(false);

	newObj->TerrainItemPtr = itemPtr;								// keep ptr to item list

			/* SET COLLISION STUFF */

	newObj->CType 			= CTYPE_MISC|CTYPE_AVOID;
	newObj->CBits			= CBITS_ALLSOLID;
	CreateCollisionBoxFromBoundingBox(newObj,.8,1);

	return(true);													// item was added
}



/************************* ADD BRONTO NECK *********************************/

Boolean AddBrontoNeck(TerrainItemEntryType *itemPtr, long  x, long z)
{
ObjNode	*newObj;


	gNewObjectDefinition.type 		= SKELETON_TYPE_BRONTONECK;
	gNewObjectDefinition.animNum	= 0;
	gNewObjectDefinition.coord.x 	= x;
	gNewObjectDefinition.coord.z 	= z;
	gNewObjectDefinition.coord.y 	= GetTerrainY(x,z) + 500.0f;
	gNewObjectDefinition.flags 		= gAutoFadeStatusBits;
	gNewObjectDefinition.slot 		= SLOT_OF_DUMB+2;
	gNewObjectDefinition.moveCall 	= MoveStaticObject;
	gNewObjectDefinition.rot 		= PI2 * ((float)itemPtr->parm[0] * (1.0f/8.0f));
	gNewObjectDefinition.scale 		= 50.0;
	newObj = MakeNewSkeletonObject(&gNewObjectDefinition);
	if (newObj == nil)
		return(false);

	newObj->TerrainItemPtr = itemPtr;								// keep ptr to item list


	return(true);													// item was added
}


/************************* ADD ROCK OVERHANG *********************************/

Boolean AddRockOverhang(TerrainItemEntryType *itemPtr, long  x, long z)
{
ObjNode	*newObj;

	switch(gTrackNum)
	{
		case	TRACK_NUM_DESERT:
				gNewObjectDefinition.type 		= DESERT_ObjType_RockOverhang  + itemPtr->parm[1];
				break;

		case	TRACK_NUM_ICE:
				gNewObjectDefinition.type 		= ICE_ObjType_IceBridge;
				break;

		default:
				return(true);
	}


	gNewObjectDefinition.group 		= MODEL_GROUP_LEVELSPECIFIC;
	gNewObjectDefinition.coord.x 	= x;
	gNewObjectDefinition.coord.z 	= z;
	gNewObjectDefinition.coord.y 	= GetTerrainY(x,z);
	gNewObjectDefinition.flags 		= gAutoFadeStatusBits;
	gNewObjectDefinition.slot 		= SLOT_OF_DUMB+3;
	gNewObjectDefinition.moveCall 	= MoveStaticObject;
	gNewObjectDefinition.rot 		= PI2 * ((float)itemPtr->parm[0] * (1.0f/8.0f));
	gNewObjectDefinition.scale 		= 1.0;
	newObj = MakeNewDisplayGroupObject(&gNewObjectDefinition);
	if (newObj == nil)
		return(false);

	newObj->TerrainItemPtr = itemPtr;								// keep ptr to item list


	return(true);													// item was added
}



/************************* ADD RICKSHAW *********************************/

Boolean AddRickshaw(TerrainItemEntryType *itemPtr, long  x, long z)
{
ObjNode	*newObj;


	gNewObjectDefinition.group 		= MODEL_GROUP_LEVELSPECIFIC;
	gNewObjectDefinition.type 		= CHINA_ObjType_Rickshaw;
	gNewObjectDefinition.coord.x 	= x;
	gNewObjectDefinition.coord.z 	= z;
	gNewObjectDefinition.coord.y 	= GetTerrainY(x,z);
	gNewObjectDefinition.flags 		= gAutoFadeStatusBits;
	gNewObjectDefinition.slot 		= 108;
	gNewObjectDefinition.moveCall 	= MoveStaticObject;
	gNewObjectDefinition.rot 		= 0;
	gNewObjectDefinition.scale 		= 1.0;
	newObj = MakeNewDisplayGroupObject(&gNewObjectDefinition);
	if (newObj == nil)
		return(false);

	newObj->TerrainItemPtr = itemPtr;								// keep ptr to item list

			/* SET COLLISION STUFF */

	newObj->CType 			= CTYPE_MISC|CTYPE_AVOID;
	newObj->CBits			= CBITS_ALLSOLID;
	CreateCollisionBoxFromBoundingBox_Rotated(newObj,.9,1);

	return(true);													// item was added
}


/********************** ADD AZTEC HEAD **************************/

Boolean AddAztecHead(TerrainItemEntryType *itemPtr, long  x, long z)
{
ObjNode	*newObj;


	gNewObjectDefinition.group 		= MODEL_GROUP_LEVELSPECIFIC;
	gNewObjectDefinition.type 		= AZTEC_ObjType_StoneHead;
	gNewObjectDefinition.coord.x 	= x;
	gNewObjectDefinition.coord.z 	= z;
	gNewObjectDefinition.coord.y 	= GetTerrainY(x,z);
	gNewObjectDefinition.flags 		= gAutoFadeStatusBits;
	gNewObjectDefinition.slot 		= 105;
	gNewObjectDefinition.moveCall 	= MoveStaticObject;
	gNewObjectDefinition.rot 		= PI2 * ((float)itemPtr->parm[0] * (1.0f/8.0f));
	gNewObjectDefinition.scale 		= 1.0;
	newObj = MakeNewDisplayGroupObject(&gNewObjectDefinition);
	if (newObj == nil)
		return(false);

	newObj->TerrainItemPtr = itemPtr;								// keep ptr to item list


			/* SET COLLISION STUFF */

	newObj->CType 			= CTYPE_MISC|CTYPE_AVOID;
	newObj->CBits			= CBITS_ALLSOLID;
	CreateCollisionBoxFromBoundingBox_Maximized(newObj);


	return(true);													// item was added
}


/************************* ADD CASTLE TOWER *********************************/

Boolean AddCastleTower(TerrainItemEntryType *itemPtr, long  x, long z)
{
ObjNode	*newObj;
Boolean	isSolid = itemPtr->parm[3] & 1;						// see if solid or not


	gNewObjectDefinition.group 		= MODEL_GROUP_LEVELSPECIFIC;
	gNewObjectDefinition.type 		= EUROPE_ObjType_CastleTower + itemPtr->parm[0];
	gNewObjectDefinition.coord.x 	= x;
	gNewObjectDefinition.coord.z 	= z;

	if (isSolid)
		gNewObjectDefinition.coord.y 	= GetMinTerrainY(x,z, gNewObjectDefinition.group, gNewObjectDefinition.type, 1.0);
	else
		gNewObjectDefinition.coord.y 	= GetTerrainY(x,z);

	gNewObjectDefinition.flags 		= gAutoFadeStatusBits|STATUS_BIT_NOTEXTUREWRAP|STATUS_BIT_CLIPALPHA;

	if (isSolid)
		gNewObjectDefinition.slot 		= 40;
	else
		gNewObjectDefinition.slot 		= SLOT_OF_DUMB+4;

	gNewObjectDefinition.moveCall 	= MoveStaticObject;
	gNewObjectDefinition.rot 		= PI2 * ((float)itemPtr->parm[0] * (1.0f/8.0f));;
	gNewObjectDefinition.scale 		= 1.0;
	newObj = MakeNewDisplayGroupObject(&gNewObjectDefinition);
	if (newObj == nil)
		return(false);

	newObj->TerrainItemPtr = itemPtr;								// keep ptr to item list

			/* SET COLLISION STUFF */

	if (isSolid)
	{
		newObj->CType 			= CTYPE_MISC|CTYPE_AVOID;
		newObj->CBits			= CBITS_ALLSOLID;
		CreateCollisionBoxFromBoundingBox_Maximized(newObj);
	}

	return(true);													// item was added
}


/************************** ADD HOUSE *******************************/

typedef struct
{
	short 	type[4];
	float	tweakXZ[4],tweakY[4];
}HouseInfo;

Boolean AddHouse(TerrainItemEntryType *itemPtr, long  x, long z)
{
ObjNode	*newObj;
static const HouseInfo info[NUM_TRACKS] =
{
	[TRACK_NUM_DESERT] =
	{
		{0,0,0,0},
		{1,1,1,1},
		{1,1,1,1},
	},

	[TRACK_NUM_JUNGLE] =
	{
		{JUNGLE_ObjType_Hut1,JUNGLE_ObjType_Hut2,0,0},
		{.9,.9,1,1},
		{1,1,1,1},
	},

	[TRACK_NUM_ICE] =
	{
		{ICE_ObjType_Igloo,0,0,0},
		{1,1,1,1},
		{1,1,1,1},
	},

	[TRACK_NUM_CRETE] =
	{
		{CRETE_ObjType_House1,CRETE_ObjType_House2,CRETE_ObjType_Palace,0},
		{1,1,1,1},
		{1,1,1,1},
	},

	[TRACK_NUM_CHINA] =
	{
		{CHINA_ObjType_House,0,0,0},
		{.9,1,1,1},
		{1,1,1,1},
	},

	[TRACK_NUM_EGYPT] =
	{
		{0,0,0,0},
		{1,1,1,1},
		{1,1,1,1},
	},

	[TRACK_NUM_EUROPE] =
	{
		{EUROPE_ObjType_Cottage,EUROPE_ObjType_Lodge,EUROPE_ObjType_TownHouse,0},
		{1,1,1,1},
		{1,1,1,1},
	},

	[TRACK_NUM_SCANDINAVIA] =
	{
		{SCANDINAVIA_ObjType_Cabin1,SCANDINAVIA_ObjType_Cabin2,SCANDINAVIA_ObjType_Cabin3,0},
		{1,1,1,1},
		{1,1,1,1},
	},

	[TRACK_NUM_ATLANTIS] =
	{
		{ATLANTIS_ObjType_BugDome,ATLANTIS_ObjType_SaucerDome,ATLANTIS_ObjType_TwinkieDome,0},
		{1,1,1,1},
		{.55,.5,.55,1},
	},
};

Boolean	notSolid = itemPtr->parm[3] & 1;						// see if solid or not
short	type = itemPtr->parm[0];

	gNewObjectDefinition.group 		= MODEL_GROUP_LEVELSPECIFIC;
	gNewObjectDefinition.type 		= info[gTrackNum].type[type];
	gNewObjectDefinition.coord.x 	= x;
	gNewObjectDefinition.coord.z 	= z;
	gNewObjectDefinition.coord.y 	= GetMinTerrainY(x,z, gNewObjectDefinition.group, gNewObjectDefinition.type, 1.0);

	if ((gTrackNum == TRACK_NUM_JUNGLE) && (gNewObjectDefinition.type == JUNGLE_ObjType_Hut1))
		gNewObjectDefinition.flags 		= gAutoFadeStatusBits | STATUS_BIT_KEEPBACKFACES | STATUS_BIT_CLIPALPHA;
	else
		gNewObjectDefinition.flags 		= gAutoFadeStatusBits | STATUS_BIT_CLIPALPHA;

	if (notSolid)
		gNewObjectDefinition.slot 		= SLOT_OF_DUMB+2;
	else
		gNewObjectDefinition.slot 		= 77;

	gNewObjectDefinition.moveCall 	= MoveStaticObject;
	gNewObjectDefinition.rot 		= PI2 * ((float)itemPtr->parm[1] * (1.0f/8.0f));
	gNewObjectDefinition.scale 		= 1.0;
	newObj = MakeNewDisplayGroupObject(&gNewObjectDefinition);
	if (newObj == nil)
		return(false);

	newObj->TerrainItemPtr = itemPtr;								// keep ptr to item list

			/* SET COLLISION STUFF */

	if (!notSolid)
	{
		newObj->CType 			= CTYPE_MISC|CTYPE_AVOID;
		newObj->CBits			= CBITS_ALLSOLID;
		CreateCollisionBoxFromBoundingBox_Rotated(newObj,info[gTrackNum].tweakXZ[type],info[gTrackNum].tweakY[type]);
	}

	return(true);													// item was added
}


/********************** ADD WELL **************************/

Boolean AddWell(TerrainItemEntryType *itemPtr, long  x, long z)
{
ObjNode	*newObj;


	gNewObjectDefinition.group 		= MODEL_GROUP_LEVELSPECIFIC;
	gNewObjectDefinition.type 		= EUROPE_ObjType_Well;
	gNewObjectDefinition.coord.x 	= x;
	gNewObjectDefinition.coord.z 	= z;
	gNewObjectDefinition.coord.y 	= GetMinTerrainY(x,z, gNewObjectDefinition.group, gNewObjectDefinition.type, 1.0);
	gNewObjectDefinition.flags 		= gAutoFadeStatusBits;
	gNewObjectDefinition.slot 		= 222;
	gNewObjectDefinition.moveCall 	= MoveStaticObject;
	gNewObjectDefinition.rot 		= 0;
	gNewObjectDefinition.scale 		= 1.0;
	newObj = MakeNewDisplayGroupObject(&gNewObjectDefinition);
	if (newObj == nil)
		return(false);

	newObj->TerrainItemPtr = itemPtr;								// keep ptr to item list


			/* SET COLLISION STUFF */

	newObj->CType 			= CTYPE_MISC|CTYPE_AVOID;
	newObj->CBits			= CBITS_ALLSOLID;
	CreateCollisionBoxFromBoundingBox(newObj,1,1);


	return(true);													// item was added
}


/********************** ADD CLOCK **************************/

Boolean AddClock(TerrainItemEntryType *itemPtr, long  x, long z)
{
ObjNode	*newObj;


	gNewObjectDefinition.group 		= MODEL_GROUP_LEVELSPECIFIC;
	gNewObjectDefinition.type 		= CRETE_ObjType_Clock;
	gNewObjectDefinition.coord.x 	= x;
	gNewObjectDefinition.coord.z 	= z;
	gNewObjectDefinition.coord.y 	= GetMinTerrainY(x,z, gNewObjectDefinition.group, gNewObjectDefinition.type, 1.0);
	gNewObjectDefinition.flags 		= gAutoFadeStatusBits;
	gNewObjectDefinition.slot 		= 420;
	gNewObjectDefinition.moveCall 	= MoveStaticObject;
	gNewObjectDefinition.rot 		= 0;
	gNewObjectDefinition.scale 		= 1.0;
	newObj = MakeNewDisplayGroupObject(&gNewObjectDefinition);
	if (newObj == nil)
		return(false);

	newObj->TerrainItemPtr = itemPtr;								// keep ptr to item list


			/* SET COLLISION STUFF */

	newObj->CType 			= CTYPE_MISC|CTYPE_AVOID;
	newObj->CBits			= CBITS_ALLSOLID;
	CreateCollisionBoxFromBoundingBox(newObj,1,1);


	return(true);													// item was added
}


/********************** ADD CLAM **************************/

Boolean AddClam(TerrainItemEntryType *itemPtr, long  x, long z)
{
ObjNode	*newObj;


	gNewObjectDefinition.group 		= MODEL_GROUP_LEVELSPECIFIC;
	gNewObjectDefinition.type 		= ATLANTIS_ObjType_Clam;
	gNewObjectDefinition.coord.x 	= x;
	gNewObjectDefinition.coord.z 	= z;
	gNewObjectDefinition.coord.y 	= GetTerrainY(x,z);
	gNewObjectDefinition.flags 		= 0;
	gNewObjectDefinition.slot 		= 200;
	gNewObjectDefinition.moveCall 	= MoveStaticObject;
	gNewObjectDefinition.rot 		= RandomFloat() * PI2;
	gNewObjectDefinition.scale 		= 1.0;
	newObj = MakeNewDisplayGroupObject(&gNewObjectDefinition);
	if (newObj == nil)
		return(false);

	newObj->TerrainItemPtr = itemPtr;								// keep ptr to item list


			/* SET COLLISION STUFF */

	newObj->CType 			= CTYPE_MISC|CTYPE_AVOID;
	newObj->CBits			= CBITS_ALLSOLID;
	CreateCollisionBoxFromBoundingBox(newObj,1,1);


	return(true);													// item was added
}



/********************** ADD FLAG POLE **************************/

Boolean AddFlagPole(TerrainItemEntryType *itemPtr, long  x, long z)
{
ObjNode	*newObj;


	gNewObjectDefinition.type 		= SKELETON_TYPE_FLAG;
	gNewObjectDefinition.animNum	= 0;
	gNewObjectDefinition.coord.x 	= x;
	gNewObjectDefinition.coord.z 	= z;
	gNewObjectDefinition.coord.y 	= GetTerrainY(x,z) - gObjectGroupBBoxList[MODEL_GROUP_SKELETONBASE+gNewObjectDefinition.type][0].min.y;

	gNewObjectDefinition.flags 		= gAutoFadeStatusBits|STATUS_BIT_CLIPALPHA;
	gNewObjectDefinition.slot 		= 285;
	gNewObjectDefinition.moveCall 	= MoveStaticObject;
	gNewObjectDefinition.rot 		= PI2 * ((float)itemPtr->parm[0] * (1.0f/8.0f));
	gNewObjectDefinition.scale 		= 10.0;
	newObj = MakeNewSkeletonObject(&gNewObjectDefinition);
	if (newObj == nil)
		return(false);

	newObj->TerrainItemPtr = itemPtr;								// keep ptr to item list

	newObj->Skeleton->AnimSpeed = 1.3f + RandomFloat();

			/* SET COLLISION STUFF */

	newObj->CType 			= CTYPE_MISC|CTYPE_AVOID;
	newObj->CBits			= CBITS_ALLSOLID;
	SetObjectCollisionBounds(newObj, 2000, -10, -50, 50, 50, -50);


	return(true);													// item was added
}




#pragma mark -

/********************** ADD STONEHENGE **************************/

Boolean AddStoneHenge(TerrainItemEntryType *itemPtr, long  x, long z)
{
ObjNode	*newObj;
short	type = itemPtr->parm[0];

	gNewObjectDefinition.group 		= MODEL_GROUP_LEVELSPECIFIC;
	gNewObjectDefinition.type 		= STONEHENGE_ObjType_Post + type;
	gNewObjectDefinition.coord.x 	= x;
	gNewObjectDefinition.coord.z 	= z;
	gNewObjectDefinition.coord.y 	= GetMinTerrainY(x,z, gNewObjectDefinition.group, gNewObjectDefinition.type, 1.0);
	gNewObjectDefinition.flags 		= gAutoFadeStatusBits;
	gNewObjectDefinition.slot 		= 100;
	gNewObjectDefinition.moveCall 	= MoveStaticObject;
	gNewObjectDefinition.rot 		= PI2 * ((float)itemPtr->parm[1] * (1.0f/64.0f));
	gNewObjectDefinition.scale 		= 1.0;
	newObj = MakeNewDisplayGroupObject(&gNewObjectDefinition);
	if (newObj == nil)
		return(false);

	newObj->TerrainItemPtr = itemPtr;								// keep ptr to item list


			/* SET COLLISION STUFF */

	newObj->CType 			= CTYPE_MISC;
	newObj->CBits			= CBITS_ALLSOLID;

	switch(type)
	{
		case	0:			// pose
				CreateCollisionBoxFromBoundingBox(newObj,1,1);
				break;

		case	1:			// inner

				{
					CollisionBoxType *boxPtr;
					OGLPoint2D		p,p1,p2;
					OGLMatrix3x3	m;

						/* CALC COORDS OF EACH PYLON */

					OGLMatrix3x3_SetRotate(&m, -newObj->Rot.y);

					p.x = -1300;
					p.y = 0;
					OGLPoint2D_Transform(&p, &m, &p1);				// calc left
					p.x = -p.x;
					OGLPoint2D_Transform(&p, &m, &p2);				// calc right

							/* BUILD 2 COLLISION BOXES */

					AllocateCollisionBoxMemory(newObj, 2);					// alloc 2 collision boxes
					boxPtr = newObj->CollisionBoxes;						// get ptr to boxes

					boxPtr[0].left 		= newObj->Coord.x + p1.x - HENGE_PYLON_RADIUS;
					boxPtr[0].right 	= newObj->Coord.x + p1.x + HENGE_PYLON_RADIUS;
					boxPtr[0].top 		= newObj->Coord.y + 6000.0;
					boxPtr[0].bottom 	= newObj->Coord.y - 10.0f;
					boxPtr[0].back 		= newObj->Coord.z + p1.y - HENGE_PYLON_RADIUS;
					boxPtr[0].front 	= newObj->Coord.z + p1.y + HENGE_PYLON_RADIUS;

					boxPtr[1].left 		= newObj->Coord.x + p2.x - HENGE_PYLON_RADIUS;
					boxPtr[1].right 	= newObj->Coord.x + p2.x + HENGE_PYLON_RADIUS;
					boxPtr[1].top 		= newObj->Coord.y + 6000.0;
					boxPtr[1].bottom 	= newObj->Coord.y - 10.0f;
					boxPtr[1].back 		= newObj->Coord.z + p2.y - HENGE_PYLON_RADIUS;
					boxPtr[1].front 	= newObj->Coord.z + p2.y + HENGE_PYLON_RADIUS;



					KeepOldCollisionBoxes(newObj);
				}
				break;

		case	2:			// outer
				SetObjectCollisionBounds(newObj, 6000, -10, -HENGE_PYLON_RADIUS, HENGE_PYLON_RADIUS, HENGE_PYLON_RADIUS, -HENGE_PYLON_RADIUS);
				break;

	}



	return(true);													// item was added
}






/********************** ADD COLISEUM **************************/

Boolean AddColiseum(TerrainItemEntryType *itemPtr, long  x, long z)
{
ObjNode	*newObj;

	gNewObjectDefinition.group 		= MODEL_GROUP_LEVELSPECIFIC;
	gNewObjectDefinition.type 		= COLISEUM_ObjType_Wall;
	gNewObjectDefinition.coord.x 	= x;
	gNewObjectDefinition.coord.z 	= z;
	gNewObjectDefinition.coord.y 	= GetTerrainY(x,z);
	gNewObjectDefinition.flags 		= STATUS_BIT_NOLIGHTING;
	gNewObjectDefinition.slot 		= 10;
	gNewObjectDefinition.moveCall 	= MoveStaticObject;
	gNewObjectDefinition.rot 		= 0;
	gNewObjectDefinition.scale 		= 1.06;
	newObj = MakeNewDisplayGroupObject(&gNewObjectDefinition);
	if (newObj == nil)
		return(false);

	newObj->TerrainItemPtr = itemPtr;								// keep ptr to item list


	return(true);													// item was added
}






#pragma mark -

/************************* ADD VOLCANO *********************************/

Boolean AddVolcano(TerrainItemEntryType *itemPtr, long  x, long z)
{
ObjNode	*newObj;


	gNewObjectDefinition.group 		= MODEL_GROUP_LEVELSPECIFIC;
	gNewObjectDefinition.type 		= JUNGLE_ObjType_Volcano;
	gNewObjectDefinition.coord.x 	= x;
	gNewObjectDefinition.coord.z 	= z;
	gNewObjectDefinition.coord.y 	= GetTerrainY(x,z);
	gNewObjectDefinition.flags 		= gAutoFadeStatusBits;
	gNewObjectDefinition.slot 		= SLOT_OF_DUMB+4;
	gNewObjectDefinition.moveCall 	= MoveVolcano;
	gNewObjectDefinition.rot 		= 0;
	gNewObjectDefinition.scale 		= 1.0;
	newObj = MakeNewDisplayGroupObject(&gNewObjectDefinition);
	if (newObj == nil)
		return(false);

	newObj->TerrainItemPtr = itemPtr;								// keep ptr to item list

	newObj->SmokeTimer= 0;

	return(true);													// item was added
}


/********************* MOVE VOLCANO *******************************/

static void MoveVolcano(ObjNode *theNode)
{
float				top,x,z;
float				fps = gFramesPerSecondFrac;
int					particleGroup,magicNum,i;
NewParticleGroupDefType	groupDef;
NewParticleDefType	newParticleDef;
OGLVector3D			d;
OGLPoint3D			p;

	if (TrackTerrainItem(theNode))							// just check to see if it's gone
	{
		DeleteObject(theNode);
		return;
	}

	top = theNode->Coord.y + gObjectGroupBBoxList[theNode->Group][theNode->Type].max.y;
	x = theNode->Coord.x;
	z = theNode->Coord.z;

		/**************/
		/* MAKE SMOKE */
		/**************/

	if (gFramesPerSecond > 15.0f)										// only do smoke if running at good frame rate
	{
		theNode->SmokeTimer -= fps;													// see if add smoke
		if (theNode->SmokeTimer <= 0.0f)
		{
			theNode->SmokeTimer += 0.06;										// reset timer

			particleGroup 	= theNode->SmokeParticleGroup;
			magicNum 		= theNode->SmokeParticleMagic;

			if ((particleGroup == -1) || (!VerifyParticleGroupMagicNum(particleGroup, magicNum)))
			{

				theNode->SmokeParticleMagic = magicNum = MyRandomLong();			// generate a random magic num

				groupDef.magicNum				= magicNum;
				groupDef.type					= PARTICLE_TYPE_FALLINGSPARKS;
				groupDef.flags					= PARTICLE_FLAGS_DONTCHECKGROUND;
				groupDef.gravity				= 0;
				groupDef.magnetism				= 0;
				groupDef.baseScale				= 100;
				groupDef.decayRate				=  -.2f;
				groupDef.fadeRate				= .1;
				groupDef.particleTextureNum		= PARTICLE_SObjType_BlackSmoke;
				groupDef.srcBlend				= GL_SRC_ALPHA;
				groupDef.dstBlend				= GL_ONE_MINUS_SRC_ALPHA;
				theNode->SmokeParticleGroup = particleGroup = NewParticleGroup(&groupDef);
			}

			if (particleGroup != -1)
			{
				for (i = 0; i < 5; i++)
				{
					p.x = x + RandomFloat2() * 400.0f;
					p.y = top + RandomFloat() * 150.0f;
					p.z = z + RandomFloat2() * 400.0f;

					d.x = RandomFloat2() * 200.0f;
					d.y = 200.0f + RandomFloat() * 500.0f;
					d.z = RandomFloat2() * 200.0f;

					newParticleDef.groupNum		= particleGroup;
					newParticleDef.where		= &p;
					newParticleDef.delta		= &d;
					newParticleDef.scale		= RandomFloat() + 1.0f;
					newParticleDef.rotZ			= RandomFloat() * PI2;
					newParticleDef.rotDZ		= RandomFloat2();
					newParticleDef.alpha		= .8;
					if (AddParticleToGroup(&newParticleDef))
					{
						theNode->SmokeParticleGroup = -1;
						break;
					}
				}
			}
		}
	}


}

#pragma mark -

/************************* ADD TORCHPOT *********************************/

Boolean AddTorchPot(TerrainItemEntryType *itemPtr, long  x, long z)
{
ObjNode	*newObj;


	gNewObjectDefinition.group 		= MODEL_GROUP_LEVELSPECIFIC;
	gNewObjectDefinition.type 		= SCANDINAVIA_ObjType_TorchPot;
	gNewObjectDefinition.coord.x 	= x;
	gNewObjectDefinition.coord.z 	= z;
	gNewObjectDefinition.coord.y 	= GetTerrainY(x,z);
	gNewObjectDefinition.flags 		= gAutoFadeStatusBits;
	gNewObjectDefinition.slot 		= 400;
	gNewObjectDefinition.moveCall 	= MoveTorchPot;
	gNewObjectDefinition.rot 		= 0;
	gNewObjectDefinition.scale 		= 1.0;
	newObj = MakeNewDisplayGroupObject(&gNewObjectDefinition);
	if (newObj == nil)
		return(false);

	newObj->TerrainItemPtr = itemPtr;								// keep ptr to item list

			/* SET COLLISION STUFF */

	newObj->CType 			= CTYPE_MISC|CTYPE_AVOID;
	newObj->CBits			= CBITS_ALLSOLID;
	CreateCollisionBoxFromBoundingBox(newObj,1,1);

	return(true);													// item was added
}


/********************* MOVE TORCHPOT **********************/

static void MoveTorchPot(ObjNode *theNode)
{

	if (TrackTerrainItem(theNode))							// just check to see if it's gone
	{
		DeleteObject(theNode);
		return;
	}

	BurnFire(theNode, theNode->Coord.x, theNode->Coord.y + 1000.0f, theNode->Coord.z, true, PARTICLE_SObjType_Fire, 2.0);

}

#pragma mark -

/************************ PRIME POLARBEAR *************************/

Boolean PrimePolarBear(long splineNum, SplineItemType *itemPtr)
{
ObjNode			*newObj;
float			x,z,placement;

			/* GET SPLINE INFO */

	placement = itemPtr->placement;
	GetCoordOnSpline(&(*gSplineList)[splineNum], placement, &x, &z);



		/* MAKE OBJECT */

	gNewObjectDefinition.type 		= SKELETON_TYPE_POLARBEAR;
	gNewObjectDefinition.animNum	= 0;
	gNewObjectDefinition.coord.x 	= x;
	gNewObjectDefinition.coord.y 	= GetTerrainY(x,z);
	gNewObjectDefinition.coord.z 	= z;
	gNewObjectDefinition.flags 		= STATUS_BIT_ONSPLINE|gAutoFadeStatusBits;
	gNewObjectDefinition.slot 		= 168;
	gNewObjectDefinition.rot 		= 0;
	gNewObjectDefinition.scale 		= BEAR_SCALE;

	newObj = MakeNewSkeletonObject(&gNewObjectDefinition);
	if (newObj == nil)
		return(false);

	DetachObject(newObj);									// detach this object from the linked list


	newObj->Skeleton->AnimSpeed = 1.5;


				/* SET MORE INFO */

	newObj->SplineItemPtr 	= itemPtr;
	newObj->SplineNum 		= splineNum;
	newObj->SplinePlacement = placement;
	newObj->SplineMoveCall 	= MovePolarBear;						// set move call
	newObj->CType			= CTYPE_MISC|CTYPE_AVOID;
	newObj->CBits			= CBITS_ALLSOLID;

	CreateCollisionBoxFromBoundingBox(newObj,1,1);

	AttachShadowToObject(newObj, SHADOW_TYPE_CIRCULAR, 20, 30, false);


			/* ADD SPLINE OBJECT TO SPLINE OBJECT LIST */

	AddToSplineObjectList(newObj);
	return(true);
}


/******************** MOVE POLARBEAR ON SPLINE ***************************/

static void MovePolarBear(ObjNode *theNode)
{
Boolean isVisible;

	isVisible = IsSplineItemVisible(theNode);					// update its visibility


		/* MOVE ALONG THE SPLINE */

	IncreaseSplineIndex(theNode, 55);

	GetObjectCoordOnSpline(theNode, &theNode->Coord.x, &theNode->Coord.z);


			/***************************/
			/* UPDATE STUFF IF VISIBLE */
			/***************************/

	if (isVisible)
	{
		theNode->Rot.y = CalcYAngleFromPointToPoint(theNode->Rot.y, theNode->OldCoord.x, theNode->OldCoord.z,	// calc y rot aim
												theNode->Coord.x, theNode->Coord.z);

		theNode->Coord.y = GetTerrainY(theNode->Coord.x, theNode->Coord.z) - theNode->BottomOff;	// get ground Y
		UpdateObjectTransforms(theNode);												// update transforms
		CalcObjectBoxFromNode(theNode);													// update collision box
		theNode->Delta.x = (theNode->Coord.x - theNode->OldCoord.x) * gFramesPerSecond;	// calc deltas
		theNode->Delta.y = (theNode->Coord.y - theNode->OldCoord.y) * gFramesPerSecond;
		theNode->Delta.z = (theNode->Coord.z - theNode->OldCoord.z) * gFramesPerSecond;
		UpdateShadow(theNode);
	}
}


#pragma mark -

/********************** ADD FLOWER **************************/

Boolean AddFlower(TerrainItemEntryType *itemPtr, long  x, long z)
{
ObjNode	*newObj;


	gNewObjectDefinition.type 		= SKELETON_TYPE_FLOWER;
	gNewObjectDefinition.animNum	= 0;
	gNewObjectDefinition.coord.x 	= x;
	gNewObjectDefinition.coord.z 	= z;
	gNewObjectDefinition.coord.y 	= GetTerrainY(x,z);

	gNewObjectDefinition.flags 		= gAutoFadeStatusBits|STATUS_BIT_CLIPALPHA;
	gNewObjectDefinition.slot 		= 70;
	gNewObjectDefinition.moveCall 	= MoveStaticObject;
	gNewObjectDefinition.rot 		= RandomFloat() * PI2;
	gNewObjectDefinition.scale 		= 20.0;
	newObj = MakeNewSkeletonObject(&gNewObjectDefinition);
	if (newObj == nil)
		return(false);

	newObj->TerrainItemPtr = itemPtr;								// keep ptr to item list

	newObj->Skeleton->AnimSpeed = 1.0f + RandomFloat() * .5f;

			/* SET COLLISION STUFF */

	newObj->CType 			= CTYPE_MISC|CTYPE_AVOID;
	newObj->CBits			= CBITS_ALLSOLID;
	SetObjectCollisionBounds(newObj, 1000, -10, -80, 80, 80, -80);


	return(true);													// item was added
}

#pragma mark -

/************************ PRIME VIKING *************************/

Boolean PrimeViking(long splineNum, SplineItemType *itemPtr)
{
ObjNode			*newObj;
float			x,z,placement;

			/* GET SPLINE INFO */

	placement = itemPtr->placement;
	GetCoordOnSpline(&(*gSplineList)[splineNum], placement, &x, &z);



		/* MAKE OBJECT */

	gNewObjectDefinition.type 		= SKELETON_TYPE_VIKING;
	gNewObjectDefinition.animNum	= 0;
	gNewObjectDefinition.coord.x 	= x;
	gNewObjectDefinition.coord.y 	= GetTerrainY(x,z);
	gNewObjectDefinition.coord.z 	= z;
	gNewObjectDefinition.flags 		= STATUS_BIT_ONSPLINE|gAutoFadeStatusBits;
	gNewObjectDefinition.slot 		= 168;
	gNewObjectDefinition.rot 		= 0;
	gNewObjectDefinition.scale 		= VIKING_SCALE;

	newObj = MakeNewSkeletonObject(&gNewObjectDefinition);
	if (newObj == nil)
		return(false);

	DetachObject(newObj);									// detach this object from the linked list


	newObj->Skeleton->AnimSpeed = 1.5;


				/* SET MORE INFO */

	newObj->SplineItemPtr 	= itemPtr;
	newObj->SplineNum 		= splineNum;
	newObj->SplinePlacement = placement;
	newObj->SplineMoveCall 	= MoveViking;						// set move call
	newObj->CType			= CTYPE_MISC|CTYPE_AVOID;
	newObj->CBits			= CBITS_ALLSOLID;

	CreateCollisionBoxFromBoundingBox(newObj,1,1);

	AttachShadowToObject(newObj, SHADOW_TYPE_CIRCULAR, 7, 7, false);


			/* ADD SPLINE OBJECT TO SPLINE OBJECT LIST */

	AddToSplineObjectList(newObj);
	return(true);
}


/******************** MOVE VIKING ON SPLINE ***************************/

static void MoveViking(ObjNode *theNode)
{
Boolean isVisible;

	isVisible = IsSplineItemVisible(theNode);					// update its visibility


		/* MOVE ALONG THE SPLINE */

	IncreaseSplineIndex(theNode, 50);

	GetObjectCoordOnSpline(theNode, &theNode->Coord.x, &theNode->Coord.z);


			/***************************/
			/* UPDATE STUFF IF VISIBLE */
			/***************************/

	if (isVisible)
	{
		theNode->Rot.y = CalcYAngleFromPointToPoint(theNode->Rot.y, theNode->OldCoord.x, theNode->OldCoord.z,	// calc y rot aim
												theNode->Coord.x, theNode->Coord.z);

		theNode->Coord.y = GetTerrainY(theNode->Coord.x, theNode->Coord.z) - theNode->BottomOff;	// get ground Y
		UpdateObjectTransforms(theNode);												// update transforms
		CalcObjectBoxFromNode(theNode);													// update collision box
		theNode->Delta.x = (theNode->Coord.x - theNode->OldCoord.x) * gFramesPerSecond;	// calc deltas
		theNode->Delta.y = (theNode->Coord.y - theNode->OldCoord.y) * gFramesPerSecond;
		theNode->Delta.z = (theNode->Coord.z - theNode->OldCoord.z) * gFramesPerSecond;
		UpdateShadow(theNode);
	}
}

