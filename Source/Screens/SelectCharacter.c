/****************************/
/*   	SELECT CHARACTER.C 	*/
/* By Brian Greenstone      */
/* (c)2000 Pangea Software  */
/* (c)2022 Iliyas Jorio     */
/****************************/


/****************************/
/*    EXTERNALS             */
/****************************/

#include "game.h"

/****************************/
/*    PROTOTYPES            */
/****************************/

static void SetupCharacterSelectScreen(short whichPlayer);
static Boolean DoCharacterSelectControls(short whichPlayer, Boolean allowAborting);
static void FreeCharacterSelectArt(void);



/****************************/
/*    CONSTANTS             */
/****************************/

#define	ARROW_SCALE		.5
#define ARROW_2D_SPREAD		276.0f
#define ARROW_Y				204.0f

#define GetCharacterArrowHomeX() (ARROW_2D_SPREAD * (gSelectedCharacterIndex - 0.5f))


/*********************/
/*    VARIABLES      */
/*********************/

static int		gSelectedCharacterIndex;

static ObjNode	*gSex[2];
static ObjNode	*gCharacterArrow;


/********************** DO CHARACTER SELECT SCREEN **************************/
//
// Return true if user aborts.
//

Boolean DoCharacterSelectScreen(short whichPlayer, Boolean allowAborting)
{
	SetupCharacterSelectScreen(whichPlayer);
	MakeFadeEvent(true);


				/*************/
				/* MAIN LOOP */
				/*************/

	CalcFramesPerSecond();
	ReadKeyboard();

	while(true)
	{
			/* SEE IF MAKE SELECTION */

		if (DoCharacterSelectControls(whichPlayer, allowAborting))
			break;


			/**************/
			/* DRAW STUFF */
			/**************/

		CalcFramesPerSecond();
		ReadKeyboard();
		MoveObjects();
		OGL_DrawScene(DrawObjects);
	}

			/***********/
			/* CLEANUP */
			/***********/

	OGL_FadeOutScene(DrawObjects, MoveObjects);
	FreeCharacterSelectArt();
	OGL_DisposeGameView();

	if (gSelectedCharacterIndex == -1)										// see if user bailed
		return(true);


		/* SET CHARACTER TYPE SELECTED */

	gPlayerInfo[whichPlayer].sex = gSelectedCharacterIndex;

	return(false);
}


/********************* SETUP CHARACTERSELECT SCREEN **********************/

static void SetupCharacterSelectScreen(short whichPlayer)
{
OGLSetupInputType	viewDef;
OGLColorRGBA		ambientColor = { .5, .5, .5, 1 };
OGLColorRGBA		fillColor1 = { 1.0, 1.0, 1.0, 1 };
OGLVector3D			fillDirection1 = { .9, -.3, -1 };
ObjNode	*multiplayerText = NULL;

	gSelectedCharacterIndex = 0;

			/**************/
			/* SETUP VIEW */
			/**************/

	OGL_NewViewDef(&viewDef);

	viewDef.camera.fov 				= .3;
	viewDef.camera.hither 			= 10;
	viewDef.camera.yon 				= 3000;
	viewDef.camera.from[0].z		= 700;

	viewDef.view.clearColor 		= (OGLColorRGBA) { .51f, .39f, .27f, 1 };
	viewDef.styles.useFog			= false;
	viewDef.view.pillarboxRatio	= PILLARBOX_RATIO_4_3;

	viewDef.lights.ambientColor 	= ambientColor;
	viewDef.lights.numFillLights 	= 1;
	viewDef.lights.fillDirection[0] = fillDirection1;
	viewDef.lights.fillColor[0] 	= fillColor1;

	OGL_SetupGameView(&viewDef);



				/************/
				/* LOAD ART */
				/************/


			/* MAKE BACKGROUND PICTURE OBJECT */

	MakeBackgroundPictureObject(":images:CharSelectScreen.jpg");


			/* LOAD SPRITES */

	LoadSpriteGroup(SPRITE_GROUP_MAINMENU, "menus", 0);


			/* LOAD SKELETONS */

	LoadASkeleton(SKELETON_TYPE_MALESTANDING);
	LoadASkeleton(SKELETON_TYPE_FEMALESTANDING);



			/*****************/
			/* BUILD OBJECTS */
			/*****************/


			/* SEE IF DOING 2-PLAYER LOCALLY */

	if (gNumLocalPlayers > 1)
	{
		NewObjectDefinitionType newObjDef =
		{
			.coord = {0, -192, 0},
			.scale = .55,
			.slot = 99
		};
		multiplayerText = TextMesh_New(GetPlayerNameWithInputDeviceHint(whichPlayer), kTextMeshAlignCenter, &newObjDef);

		multiplayerText->ColorFilter = (OGLColorRGBA) {.2, .7, .2, 1};
	}
	else
	{
			/* CREATE NAME STRINGS */

		NewObjectDefinitionType newObjDef_NameString =
		{
			.coord = {-0.5f*ARROW_2D_SPREAD, -192, 0},
			.scale = .6f,
			.slot = 99
		};
		TextMesh_New(Localize(STR_BROG), kTextMeshAlignCenter, &newObjDef_NameString);

		newObjDef_NameString.coord.x 	= 0.5f*ARROW_2D_SPREAD;
		TextMesh_New(Localize(STR_GRAG), kTextMeshAlignCenter, &newObjDef_NameString);
	}


			/* CREATE MALE CHARACTER */

	NewObjectDefinitionType newObjDef_Character =
	{
		.type = SKELETON_TYPE_MALESTANDING,
		.animNum = 1,
		.coord = {-60, 0, 0},
		.slot = 100,
		.rot = PI,
		.scale = .5f
	};
	gSex[0] = MakeNewSkeletonObject(&newObjDef_Character);

			/* CREATE FEMALE CHARACTER */

	newObjDef_Character.type 		= SKELETON_TYPE_FEMALESTANDING;
	newObjDef_Character.coord.x 	= -newObjDef_Character.coord.x;
	newObjDef_Character.animNum	= 0;
	gSex[1] = MakeNewSkeletonObject(&newObjDef_Character);

			/* CREATE ARROW */

	{
		NewObjectDefinitionType def =
		{
			.group = SPRITE_GROUP_MAINMENU,
			.type = MENUS_SObjType_UpArrow,
			.coord = { GetCharacterArrowHomeX(), ARROW_Y, 0},
			.slot = SPRITE_SLOT,
			.scale = ARROW_SCALE,
		};
		gCharacterArrow = MakeSpriteObject(&def);
	}


				/* SET UP CHARACTER SKINS */

	LoadCavemanSkins();

	int skinID = gPlayerInfo[whichPlayer].skin;

	if ((gGameMode == GAME_MODE_PRACTICE || gGameMode == GAME_MODE_TOURNAMENT)
		&& skinID == 0)
	{
		// Single-player mode: if the player didn't change their skin,
		// keep stock model skins so we get a chance to show off Grag's
		// trademark purple outfit (even though it's actually unused in-game).
	}
	else
	{
				/* OVERRIDE CHARACTER SKINS */

		gSex[0]->Skeleton->overrideTexture = gCavemanSkins[0][skinID];
		gSex[1]->Skeleton->overrideTexture = gCavemanSkins[1][skinID];

		if (gGameMode == GAME_MODE_CAPTUREFLAG)
		{
			int team = gPlayerInfo[whichPlayer].team;
			if (team == RED_TEAM)
			{
				multiplayerText->ColorFilter = (OGLColorRGBA) {.8, 0, 0, 1};
			}
			else
			{
				multiplayerText->ColorFilter = (OGLColorRGBA) {0, .8, 0, 1};
			}
		}
	}
}



/********************** FREE CHARACTERSELECT ART **********************/

static void FreeCharacterSelectArt(void)
{
	DeleteAllObjects();
	FreeAllSkeletonFiles(-1);
	DisposeAllBG3DContainers();
}


/***************** CYCLE CHARACTER SKINS *******************/

static void CycleSkin(short whichPlayer, int delta)
{
	GAME_ASSERT(whichPlayer < MAX_PLAYERS);

			/* FIND OUT WHICH SKINS ARE ALREADY TAKEN */

	uint32_t skinsTaken = 0;

	if (!gNetGameInProgress)		// in net games, let user pick any skin
	{
		for (int prevPlayer = 0; prevPlayer < whichPlayer; prevPlayer++)
		{
			skinsTaken |= (1 << gPlayerInfo[prevPlayer].skin);
		}
	}

			/* CYCLE TO NEXT AVAILABLE SKIN */

	short oldSkin = gPlayerInfo[whichPlayer].skin;
	short newSkin = oldSkin;

	do
	{
		newSkin += delta;
		newSkin = PositiveModulo(newSkin, NUM_CAVEMAN_SKINS);
	} while (skinsTaken & (1 << newSkin));

	gPlayerInfo[whichPlayer].skin = newSkin;

			/* SET TEXTURES */

	gSex[0]->Skeleton->overrideTexture = gCavemanSkins[0][newSkin];
	gSex[1]->Skeleton->overrideTexture = gCavemanSkins[1][newSkin];

			/* SWAP MY OLD SKIN W/ PLAYER THAT USES THE ONE I WANT */

	for (int i = 0; i < MAX_PLAYERS; i++)
	{
		if (i != whichPlayer && gPlayerInfo[i].skin == newSkin)
		{
			gPlayerInfo[i].skin = oldSkin;
			break;
		}
	}
}


/***************** DO CHARACTERSELECT CONTROLS *******************/

static Boolean DoCharacterSelectControls(short whichPlayer, Boolean allowAborting)
{
short	p;


	if (gNetGameInProgress)										// if net game, then use P0 controls
		p = 0;
	else
		p = whichPlayer;										// otherwise, use P0 or P1 controls depending.


		/* SEE IF ABORT */

	if (allowAborting && GetNewNeedStateAnyP(kNeed_UIBack))		// anyone can abort
	{
		PlayEffect(EFFECT_GETPOW);
		gSelectedCharacterIndex = -1;
		return(true);
	}

		/* SEE IF SELECT THIS ONE */

	if (GetNewNeedState(kNeed_UIConfirm, p))
	{
		PlayEffect_Parms(EFFECT_SELECTCLICK, FULL_CHANNEL_VOLUME, FULL_CHANNEL_VOLUME, NORMAL_CHANNEL_RATE * 2/3);
		return(true);
	}
	else
	if (IsCheatKeyComboDown())		// useful to test local multiplayer without having all controllers plugged in
	{
		PlayEffect(EFFECT_ROMANCANDLE_LAUNCH);
		return true;
	}


		/* SEE IF CHANGE SELECTION */

	if (GetNewNeedState(kNeed_UILeft, p) && (gSelectedCharacterIndex > 0))
	{
		PlayEffect(EFFECT_SELECTCLICK);
		gSelectedCharacterIndex--;
		MorphToSkeletonAnim(gSex[0]->Skeleton, 1, 5.0);
		MorphToSkeletonAnim(gSex[1]->Skeleton, 0, 5.0);
		gCharacterArrow->Coord.x = GetCharacterArrowHomeX();
		MakeTwitch(gCharacterArrow, kTwitchPreset_DisplaceRTL);
	}
	else
	if (GetNewNeedState(kNeed_UIRight, p) && (gSelectedCharacterIndex < 1))
	{
		PlayEffect(EFFECT_SELECTCLICK);
		gSelectedCharacterIndex++;
		MorphToSkeletonAnim(gSex[0]->Skeleton, 0, 5.0);
		MorphToSkeletonAnim(gSex[1]->Skeleton, 1, 5.0);
		gCharacterArrow->Coord.x = GetCharacterArrowHomeX();
		MakeTwitch(gCharacterArrow, kTwitchPreset_DisplaceLTR);
	}
	else
	if (GetNewNeedState(kNeed_UIUp, p))
	{
		if (gGameMode != GAME_MODE_CAPTUREFLAG)
		{
			PlayEffect_Parms(EFFECT_SELECTCLICK, FULL_CHANNEL_VOLUME, FULL_CHANNEL_VOLUME, NORMAL_CHANNEL_RATE * 0.7f);
			CycleSkin(whichPlayer, 1);
		}
		else
		{
			PlayEffect(EFFECT_BADSELECT);
		}
	}
	else
	if (GetNewNeedState(kNeed_UIDown, p))
	{
		if (gGameMode != GAME_MODE_CAPTUREFLAG)
		{
			PlayEffect_Parms(EFFECT_SELECTCLICK, FULL_CHANNEL_VOLUME, FULL_CHANNEL_VOLUME, NORMAL_CHANNEL_RATE * 0.7f);
			CycleSkin(whichPlayer, -1);
		}
		else
		{
			PlayEffect(EFFECT_BADSELECT);
		}
	}

	return(false);
}


