/****************************/
/*   	INFOBAR.C		    */
/* (c)2000 Pangea Software  */
/* By Brian Greenstone      */
/****************************/


/****************************/
/*    EXTERNALS             */
/****************************/

#include "game.h"

/****************************/
/*    PROTOTYPES            */
/****************************/

static void Infobar_DrawMap(const OGLSetupOutputType *setupInfo);
static void Infobar_DrawPlace(const OGLSetupOutputType *setupInfo);
static void Infobar_DrawInventoryPOW(const OGLSetupOutputType *setupInfo);
static void Infobar_DrawWrongWay(const OGLSetupOutputType *setupInfo);
static void Infobar_DrawStartingLight(const OGLSetupOutputType *setupInfo);
static void Infobar_DrawLap(const OGLSetupOutputType *setupInfo);
static void Infobar_DrawTokens(const OGLSetupOutputType *setupInfo);
static void Infobar_DrawTimerPowerups(const OGLSetupOutputType *setupInfo);
static void Infobar_DrawTagTimer(const OGLSetupOutputType *setupInfo);
static void Infobar_DrawFlags(const OGLSetupOutputType *setupInfo);
static void Infobar_DrawHealth(const OGLSetupOutputType *setupInfo);

static void MoveFinalPlace(ObjNode *theNode);
static void MoveTrackName(ObjNode *theNode);
static void MoveLapMessage(ObjNode *theNode);
static void MovePressAnyKey(ObjNode *theNode);


/****************************/
/*    CONSTANTS             */
/****************************/

#define PLAYER_NAME_SAFE_CHARSET " .0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ"

#define OVERHEAD_MAP_REFERENCE_SIZE 256.0f

enum
{
	ICON_PLACE	 = 0,
	ICON_MAP,
	ICON_STARTLIGHT,
	ICON_LAP,
	ICON_WRONGWAY,
	ICON_TOKEN,
	ICON_WEAPON,
	ICON_TIMER,
	ICON_TIMERINDEX,
	ICON_POWTIMER,
	ICON_FIRE,

	NUM_INFOBAR_ICONTYPES
};



typedef struct
{
	float x;
	float y;
	float scale;
	float xSpacing;
	float ySpacing;
} IconPositioning;


static const IconPositioning	gIconInfo[NUM_INFOBAR_ICONTYPES][NUM_SPLITSCREEN_MODES] =
{
	[ICON_PLACE] =
	{
		[SPLITSCREEN_MODE_NONE]  = { -.80*320, -.80*240, 0.9, 0, 0 },
		[SPLITSCREEN_MODE_HORIZ] = { -.80*320, -.66*240, 0.9, 0, 0 },
		[SPLITSCREEN_MODE_VERT]  = { -.75*320, -.85*240, 1.2, 0, 0 },
	},

	[ICON_MAP] =
	{
		[SPLITSCREEN_MODE_NONE]  = { .75*320, .65*240, .20*2.5, 0, 0 },
		[SPLITSCREEN_MODE_HORIZ] = { .75*320, .50*240, .15*2.5, 0, 0 },
		[SPLITSCREEN_MODE_VERT]  = { .60*320, .75*240, .30*2.5, 0, 0 },
	},

	[ICON_STARTLIGHT] =
	{
		[SPLITSCREEN_MODE_NONE]  = { 0, -.3*240, 1.0, 0, 0 },
		[SPLITSCREEN_MODE_HORIZ] = { 0, -.3*240, 1.0, 0, 0 },
		[SPLITSCREEN_MODE_VERT]  = { 0, -.1*240, 1.4, 0, 0 },
	},

	[ICON_LAP] =
	{
		[SPLITSCREEN_MODE_NONE]  = { -.84*320, .8*240, 1.0, 0, 0 },
		[SPLITSCREEN_MODE_HORIZ] = { -.90*320, .8*240, 0.7, 0, 0 },
		[SPLITSCREEN_MODE_VERT]  = { -.85*320, .8*240, 1.7, 0, 0 },
	},

	[ICON_WRONGWAY] =
	{
		[SPLITSCREEN_MODE_NONE]  = { 0, -.5*240, 1.0, 0, 0 },
		[SPLITSCREEN_MODE_HORIZ] = { 0, -.4*240, 1.0, 0, 0 },
		[SPLITSCREEN_MODE_VERT]  = { 0, -.5*240, 1.5, 0, 0 },
	},

	[ICON_TOKEN] =
	{
		[SPLITSCREEN_MODE_NONE]  = { .4*320, -.9*240, .4, .08*320, 0 },
		[SPLITSCREEN_MODE_HORIZ] = { .4*320, -.8*240, .4, .08*320, 0 },
		[SPLITSCREEN_MODE_VERT]  = { .4*320, -.9*240, .4, .08*320, 0 },
	},

	[ICON_WEAPON] =
	{
		[SPLITSCREEN_MODE_NONE]  = { -.20*320, -.85*240, 0.9, .13*320, 0 },
		[SPLITSCREEN_MODE_HORIZ] = { -.20*320, -.78*240, 0.9, .13*320, 0 },
		[SPLITSCREEN_MODE_VERT]  = { -.22*320, -.90*240, 1.3, .17*320, 0 },
	},

	[ICON_TIMER] =
	{
		[SPLITSCREEN_MODE_NONE]  = { .63*320, -.85*240, 1.0, .39*320, 0 },
		[SPLITSCREEN_MODE_HORIZ] = { .70*320, -.70*240, 0.8, .35*320, 0 },
		[SPLITSCREEN_MODE_VERT]  = { .60*320, -.85*240, 1.2, .50*320, 0 },
	},

	[ICON_TIMERINDEX] =
	{
		[SPLITSCREEN_MODE_NONE]  = { .48*320, -.85*240, .6, .33*320, 0 },
		[SPLITSCREEN_MODE_HORIZ] = { .60*320, -.70*240, .6, .25*320, 0 },
		[SPLITSCREEN_MODE_VERT]  = { .42*320, -.85*240, .9, .40*320, 0 },
	},

	[ICON_POWTIMER] =
	{
		[SPLITSCREEN_MODE_NONE]  = { -.91*320, -.40*240,  .8, .14*320, .19*240 },
		[SPLITSCREEN_MODE_HORIZ] = { -.90*320, -.20*240,  .5, .09*320, .25*240 },
		[SPLITSCREEN_MODE_VERT]  = { -.91*320, -.40*240, 1.0, .18*320, .12*240 },
	},

	[ICON_FIRE] =
	{
		[SPLITSCREEN_MODE_NONE]  = { -.94*320, -.85*240, .5, .1*320, 0 },
		[SPLITSCREEN_MODE_HORIZ] = { -.94*320, -.78*240, .5, .1*320, 0 },
		[SPLITSCREEN_MODE_VERT]  = { -.94*320, -.90*240, .5, .1*320, 0 },
	},
};



static const struct
{
	const float* timerPtr0;
	int sprite;
} gInfobarTimers[] =
{
	{ &gPlayerInfo[0].stickyTiresTimer,			INFOBAR_SObjType_StickyTires },
	{ &gPlayerInfo[0].nitroTimer,				INFOBAR_SObjType_Weapon_Nitro },
	{ &gPlayerInfo[0].superSuspensionTimer,		INFOBAR_SObjType_Suspension },
	{ &gPlayerInfo[0].invisibilityTimer,		INFOBAR_SObjType_Invisibility },
	{ &gPlayerInfo[0].frozenTimer,				INFOBAR_SObjType_Weapon_Freeze },
	{ &gPlayerInfo[0].flamingTimer,				INFOBAR_SObjType_RedTorch },
};


/*********************/
/*    VARIABLES      */
/*********************/

static float	gMapFit = 1;

float			gStartingLightTimer;

ObjNode			*gFinalPlaceObj = nil;

Boolean			gHideInfobar = false;

ObjNode			*gWinLoseString[MAX_PLAYERS];


/********************* INIT INFOBAR ****************************/
//
// Called at beginning of level
//

void InitInfobar(OGLSetupOutputType *setupInfo)
{
static const char*	maps[] =
{
	"maps:DesertMap",
	"maps:JungleMap",
	"maps:IceMap",
	"maps:CreteMap",
	"maps:ChinaMap",
	"maps:EgyptMap",
	"maps:EuropeMap",
	"maps:ScandinaviaMap",
	"maps:AtlantisMap",
	"maps:StoneHengeMap",
	"maps:AztecMap",
	"maps:ColiseumMap",
	"maps:MazeMap",
	"maps:CelticMap",
	"maps:TarPitsMap",
	"maps:SpiralMap",
	"maps:RampsMap",
};


		/* SETUP STARTING LIGHT */

	gStartingLightTimer = 3.0;											// init starting light
	gFinalPlaceObj = nil;

	for (int i = 0; i < MAX_PLAYERS; i++)
		gWinLoseString[i] = nil;

			/* LOAD MAP SPRITE */

	LoadSpriteGroup(SPRITE_GROUP_OVERHEADMAP, maps[gTrackNum], kAtlasLoadAsSingleSprite, setupInfo);

	float mapImageHeight = GetSpriteInfo(SPRITE_GROUP_OVERHEADMAP, 1)->yadv;
	gMapFit = OVERHEAD_MAP_REFERENCE_SIZE / mapImageHeight;


			/* PUT SELF-RUNNING DEMO MESSAGE UP */

	if (gIsSelfRunningDemo)
	{
		NewObjectDefinitionType def =
		{
			.coord		= {0, 225, 0},
			.scale		= .3,
			.slot		= SPRITE_SLOT,
			.moveCall	= MovePressAnyKey,
		};

		TextMesh_New(Localize(STR_PRESS_ANY_KEY), 0, &def);
	}
}


/****************** DISPOSE INFOBAR **********************/

void DisposeInfobar(void)
{
}


/********************** DRAW INFOBAR ****************************/

void DrawInfobar(OGLSetupOutputType *setupInfo)
{
	if (gHideInfobar)
		return;

		/************/
		/* SET TAGS */
		/************/

	OGL_PushState();

	if (setupInfo->useFog)
		glDisable(GL_FOG);
	OGL_DisableLighting();
	glDisable(GL_CULL_FACE);
	glDisable(GL_DEPTH_TEST);								// no z-buffer


			/* INIT MATRICES */

	OGL_SetProjection(kProjectionType2DNDC);


		/***************/
		/* DRAW THINGS */
		/***************/


		/* DRAW THE STANDARD THINGS */

	Infobar_DrawMap(setupInfo);
	Infobar_DrawInventoryPOW(setupInfo);
	Infobar_DrawStartingLight(setupInfo);
	Infobar_DrawTimerPowerups(setupInfo);


		/* DRAW GAME MODE SPECIFICS */

	switch(gGameMode)
	{
		case	GAME_MODE_PRACTICE:
		case	GAME_MODE_MULTIPLAYERRACE:
				Infobar_DrawPlace(setupInfo);
				Infobar_DrawWrongWay(setupInfo);
				Infobar_DrawLap(setupInfo);
				break;

		case	GAME_MODE_TOURNAMENT:
				Infobar_DrawPlace(setupInfo);
				Infobar_DrawWrongWay(setupInfo);
				Infobar_DrawLap(setupInfo);
				Infobar_DrawTokens(setupInfo);
				break;

		case	GAME_MODE_TAG1:
		case	GAME_MODE_TAG2:
				Infobar_DrawTagTimer(setupInfo);
				break;

		case	GAME_MODE_SURVIVAL:
				Infobar_DrawHealth(setupInfo);
				break;

		case	GAME_MODE_CAPTUREFLAG:
				Infobar_DrawFlags(setupInfo);
				break;

	}


			/***********/
			/* CLEANUP */
			/***********/

	OGL_PopState();
}



/********************** DRAW MAP *******************************/

static void GetPointOnOverheadMap(float* px, float* pz)
{
	float x = *px;
	float z = *pz;

	float scale = gIconInfo[ICON_MAP][gActiveSplitScreenMode].scale;
	float mapX = gIconInfo[ICON_MAP][gActiveSplitScreenMode].x;
	float mapY = gIconInfo[ICON_MAP][gActiveSplitScreenMode].y;

	x /= gTerrainUnitWidth;		// get 0..1 coordinate values
	z /= gTerrainUnitDepth;

	x = x * 2.0f - 1.0f;								// convert to -1..1
	z = z * 2.0f - 1.0f;

	x *= scale * OVERHEAD_MAP_REFERENCE_SIZE * .5f;		// shrink to size of underlay map
	z *= scale * OVERHEAD_MAP_REFERENCE_SIZE * .5f;

	x += mapX;											// position it
	z += mapY;

	*px = x;
	*pz = z;
}

static void Infobar_DrawMap(const OGLSetupOutputType *setupInfo)
{
float	scale,mapX,mapY;

static const OGLColorRGBA	blipColors[] =
{
	{.8,.5,.3,.9},		// brown
	{0,1,0,.9},			// green
	{0,0,1,.9},			// blue
	{.5,.5,.5,.9},		// grey
	{1,0,0,.9},			// red
	{1,1,1,.9},			// white
};

	short	p = GetPlayerNum(gCurrentSplitScreenPane);


	if (gGameMode == GAME_MODE_TAG1)						// if in TAG 1 mode, tagged player is only one with a map
	{
		if (!gPlayerInfo[p].isIt)
			return;
	}
	else
	if (gGameMode == GAME_MODE_TAG2)						// if in TAG 2 mode, tagged player is only one without a map
	{
		if (gPlayerInfo[p].isIt)
			return;
	}


	scale = gIconInfo[ICON_MAP][gActiveSplitScreenMode].scale;
	mapX = gIconInfo[ICON_MAP][gActiveSplitScreenMode].x;
	mapY = gIconInfo[ICON_MAP][gActiveSplitScreenMode].y;


			/* DRAW THE MAP UNDERLAY */

	DrawSprite(SPRITE_GROUP_OVERHEADMAP, 1, mapX, mapY,
			scale * gMapFit,
			0,
			kTextMeshAlignCenter | kTextMeshAlignMiddle,
			setupInfo);


			/***********************/
			/* DRAW PLAYER MARKERS */
			/***********************/

	OGL_PushState();
	OGL_SetProjection(kProjectionType2DOrthoCentered);
	glDisable(GL_TEXTURE_2D);								// not textured, so disable textures

	for (int i = gNumTotalPlayers-1; i >= 0; i--)			// draw from last to first so that player 1 is always on "top"
	{
		if (p != i											// if this isnt me then see if hidden
			&& gPlayerInfo[i].invisibilityTimer > 0.0f)
		{
			continue;
		}

			/* ORIENT IT */

		float x = gPlayerInfo[i].coord.x;
		float z = gPlayerInfo[i].coord.z;
		GetPointOnOverheadMap(&x, &z);

		float scaleBasis = ((i == p) ? 10 : 7);				// draw my marker bigger
		scaleBasis *= scale;

		glPushMatrix();										// back up MODELVIEW matrix

		glTranslatef(x, z, 0);
		glScalef(scaleBasis, scaleBasis, 1);

		float rot = gPlayerInfo[i].objNode->Rot.y;
		glRotatef(180 - OGLMath_RadiansToDegrees(rot), 0, 0, 1);	// doing 180-angle because Y points down for us


			/* SET COLOR */

		switch(gGameMode)
		{
			case	GAME_MODE_TAG1:
			case	GAME_MODE_TAG2:
					if (gPlayerInfo[i].isIt)						// in tag mode, all players are white except for the tagged guy
						glColor3fv((float *)&gTagColor);
					else
						glColor3f(1,1,1);
					break;

			case	GAME_MODE_CAPTUREFLAG:							// in capture-flag mode players are team color coded red or green
					if (gPlayerInfo[i].team == 0)
						glColor3f(1,0,0);
					else
						glColor3f(0,1,0);
					break;


			default:
					glColor4fv((GLfloat *)&blipColors[i]);					// standard player colors
		}

				/* DRAW IT */

		glBegin(GL_TRIANGLES);
		glVertex3f(-1,  -1, 0);
		glVertex3f(0,   1.5, 0);
		glVertex3f(1,  -1, 0);
		glEnd();

		glColor4f(0,0,0,.8);
		glBegin(GL_LINE_LOOP);		// also outline it
		glVertex3f(-1,  -1, 0);
		glVertex3f(0,   1.5, 0);
		glVertex3f(1,  -1, 0);
		glEnd();

		glPopMatrix();									// restore MODELVIEW matrix
	}

			/**********************/
			/* DRAW TORCH MARKERS */
			/**********************/

	for (int i = 0; i < gNumTorches; i++)
	{
		if (gTorchObjs[i]->Mode == 2) 		// dont draw if this torch is at base
			continue;

		float x = gTorchObjs[i]->Coord.x;
		float z = gTorchObjs[i]->Coord.z;
		GetPointOnOverheadMap(&x, &z);

			/* ORIENT IT */

		glPushMatrix();										// back up MODELVIEW matrix

		float scaleBasis = scale * 7.0f;					// calculate a scale basis to keep things scaled relative to texture size
		glTranslatef(x,z,0);
		glScalef(scaleBasis, scaleBasis, 1);
		glRotatef(180, 0, 0, 1);


			/* SET COLOR */

		if (gTorchObjs[i]->TorchTeam)
			glColor3f(0,1,0);
		else
			glColor3f(1,.3,0);


				/* DRAW IT */

		glBegin(GL_TRIANGLES);
		glVertex3f(-1,  -1, 0);
		glVertex3f(0,   1.5, 0);
		glVertex3f(1,  -1, 0);
		glEnd();

		glPopMatrix();									// restore MODELVIEW matrix
	}

	OGL_PopState();
}


/********************** DRAW PLACE *************************/

static void Infobar_DrawPlace(const OGLSetupOutputType *setupInfo)
{
int	place,playerNum;

	playerNum = GetPlayerNum(gCurrentSplitScreenPane);
	place = gPlayerInfo[playerNum].place;

	DrawSprite(SPRITE_GROUP_INFOBAR, INFOBAR_SObjType_Place1+place,
				gIconInfo[ICON_PLACE][gActiveSplitScreenMode].x,
				gIconInfo[ICON_PLACE][gActiveSplitScreenMode].y,
				gIconInfo[ICON_PLACE][gActiveSplitScreenMode].scale,
				0, 0, setupInfo);
}


/********************** DRAW WEAPON TYPE *************************/

static void Infobar_DrawInventoryPOW(const OGLSetupOutputType *setupInfo)
{
int			n;
short		powType,q;
char		s[16];
float		x,y,scale, spacing, fontScale;

	n = GetPlayerNum(gCurrentSplitScreenPane);

	powType = gPlayerInfo[n].powType;				// get weapon type
	if (powType == POW_TYPE_NONE)
		return;

	x = gIconInfo[ICON_WEAPON][gActiveSplitScreenMode].x;
	y = gIconInfo[ICON_WEAPON][gActiveSplitScreenMode].y;
	scale = gIconInfo[ICON_WEAPON][gActiveSplitScreenMode].scale;
	spacing = gIconInfo[ICON_WEAPON][gActiveSplitScreenMode].xSpacing;

	fontScale = scale * .7f;

		/* DRAW WEAPON ICON */

	DrawSprite(SPRITE_GROUP_INFOBAR, INFOBAR_SObjType_Weapon_Bone + powType,
				x, y, scale, 0, 0, setupInfo);


		/* DRAW X-QUANTITY ICON */

	x += spacing;
	DrawSprite(SPRITE_GROUP_INFOBAR, INFOBAR_SObjType_WeaponX, x, y, scale * .8f, 0, 0, setupInfo);


		/* DRAW QUANTITY NUMBER */

	gGlobalColorFilter.r = .4;						// tint green
	gGlobalColorFilter.g = 1.0;
	gGlobalColorFilter.b = .3;

	q = gPlayerInfo[n].powQuantity;					// get weapon quantity
	snprintf(s, sizeof(s), "%d", q);

	x += spacing;
	Atlas_DrawString(SPRITE_GROUP_FONT, s, x, y, fontScale, 0, 0, setupInfo);


	gGlobalColorFilter.r = 1;
	gGlobalColorFilter.g = 1;
	gGlobalColorFilter.b = 1;

}


/********************** DRAW WRONG WAY *************************/

static void Infobar_DrawWrongWay(const OGLSetupOutputType *setupInfo)
{
Boolean	wrongWay;
short	p;

	p = GetPlayerNum(gCurrentSplitScreenPane);

	wrongWay = gPlayerInfo[p].wrongWay;

	if (wrongWay)
	{
		DrawSprite(SPRITE_GROUP_INFOBAR, INFOBAR_SObjType_WrongWay,
					gIconInfo[ICON_WRONGWAY][gActiveSplitScreenMode].x,
					gIconInfo[ICON_WRONGWAY][gActiveSplitScreenMode].y,
					gIconInfo[ICON_WRONGWAY][gActiveSplitScreenMode].scale,
					0, 0, setupInfo);
	}
}


/********************** DRAW STARTING LIGHT *************************/

static void Infobar_DrawStartingLight(const OGLSetupOutputType *setupInfo)
{
int		oldTimer;

	if (gGamePaused)
		return;

			/* CHECK TIMER */

	if (gStartingLightTimer <= 0.0f)
		return;

	oldTimer = gStartingLightTimer;

	if (gCameraStartupTimer < .2f)									// dont tick down until camera intro is about done
		gStartingLightTimer -= gFramesPerSecondFrac / (float)gNumSplitScreenPanes;
	else
		return;


			/* DRAW THE NEEDED SPRITE */

	if (gStartingLightTimer <= 1.0f)								// green
	{
		gNoCarControls = false;										// once green we have control
		DrawSprite(SPRITE_GROUP_INFOBAR, INFOBAR_SObjType_Go,
					gIconInfo[ICON_STARTLIGHT][gActiveSplitScreenMode].x,
					gIconInfo[ICON_STARTLIGHT][gActiveSplitScreenMode].y,
					gIconInfo[ICON_STARTLIGHT][gActiveSplitScreenMode].scale,
					0, 0, setupInfo);
	}
	else
	if (gStartingLightTimer <= 2.0f)								// yellow
	{
		DrawSprite(SPRITE_GROUP_INFOBAR, INFOBAR_SObjType_Set,
					gIconInfo[ICON_STARTLIGHT][gActiveSplitScreenMode].x,
					gIconInfo[ICON_STARTLIGHT][gActiveSplitScreenMode].y,
					gIconInfo[ICON_STARTLIGHT][gActiveSplitScreenMode].scale,
					0, 0, setupInfo);
	}
	else															// red
	{
		DrawSprite(SPRITE_GROUP_INFOBAR, INFOBAR_SObjType_Ready,
					gIconInfo[ICON_STARTLIGHT][gActiveSplitScreenMode].x,
					gIconInfo[ICON_STARTLIGHT][gActiveSplitScreenMode].y,
					gIconInfo[ICON_STARTLIGHT][gActiveSplitScreenMode].scale,
					0, 0, setupInfo);
	}


			/* IF CHANGED, THEN BEEP */

	if (oldTimer != (int)gStartingLightTimer)
	{
		static const short voice[] = {EFFECT_GO, EFFECT_SET, EFFECT_READY};

		PlayAnnouncerSound(voice[(int)gStartingLightTimer], true, 0);
	}
}





/********************** DRAW LAP *************************/

static void Infobar_DrawLap(const OGLSetupOutputType *setupInfo)
{
int	lap,playerNum;


			/*******************************/
			/* DRAW THE CURRENT LAP NUMBER */
			/*******************************/

	playerNum = GetPlayerNum(gCurrentSplitScreenPane);

	lap = gPlayerInfo[playerNum].lapNum;

	if (lap < 0)
		lap = 0;
	if (lap > 2)
		lap = 2;


	DrawSprite(SPRITE_GROUP_INFOBAR, INFOBAR_SObjType_Lap1of3+lap,
				gIconInfo[ICON_LAP][gActiveSplitScreenMode].x,
				gIconInfo[ICON_LAP][gActiveSplitScreenMode].y,
				gIconInfo[ICON_LAP][gActiveSplitScreenMode].scale,
				0, 0, setupInfo);
}

/********************** DRAW TOKENS *************************/

static void Infobar_DrawTokens(const OGLSetupOutputType *setupInfo)
{
short	playerNum, numTokens,i;
float	x,y,scale,spacing;

	playerNum = GetPlayerNum(gCurrentSplitScreenPane);


	numTokens = gPlayerInfo[playerNum].numTokens;

	x = gIconInfo[ICON_TOKEN][gActiveSplitScreenMode].x;
	y = gIconInfo[ICON_TOKEN][gActiveSplitScreenMode].y;
	scale = gIconInfo[ICON_TOKEN][gActiveSplitScreenMode].scale;
	spacing = gIconInfo[ICON_TOKEN][gActiveSplitScreenMode].xSpacing;

	for (i = 1; i <= MAX_TOKENS; i++)
	{
		if (i > numTokens)
			DrawSprite(	SPRITE_GROUP_INFOBAR, INFOBAR_SObjType_Token_ArrowheadDim,	x, y, scale, 0, 0, setupInfo);
		else
			DrawSprite(	SPRITE_GROUP_INFOBAR, INFOBAR_SObjType_Token_Arrowhead,	x, y, scale, 0, 0, setupInfo);

		x += spacing;
	}
}


/********************* DRAW TIMER POWERUPS **************************/

static void Infobar_DrawTimerPowerups(const OGLSetupOutputType *setupInfo)
{
short	p;
float	timer,x,y,x2, scale, fontScale, spacing, lineSpacing;
char	s[16];
static const OGLColorRGB tint = {1,.7,.5};
static const OGLColorRGB noTint = {1,1,1};

	p = GetPlayerNum(gCurrentSplitScreenPane);

	x = gIconInfo[ICON_POWTIMER][gActiveSplitScreenMode].x;
	y = gIconInfo[ICON_POWTIMER][gActiveSplitScreenMode].y;
	scale = gIconInfo[ICON_POWTIMER][gActiveSplitScreenMode].scale;
	fontScale = scale * .6f;
	spacing = gIconInfo[ICON_POWTIMER][gActiveSplitScreenMode].xSpacing;
	lineSpacing = gIconInfo[ICON_POWTIMER][gActiveSplitScreenMode].ySpacing;

	static const size_t numTimers = sizeof(gInfobarTimers) / sizeof(gInfobarTimers[0]);
	size_t offset = p * sizeof(gPlayerInfo[0]);

	for (size_t i = 0; i < numTimers; i++)
	{
		timer = *(float*) ( ((uint8_t*) gInfobarTimers[i].timerPtr0) + offset );

		if (timer <= 0.0f)
		{
			continue;
		}


			/* DRAW ICON */

		DrawSprite(SPRITE_GROUP_INFOBAR, gInfobarTimers[i].sprite, x, y, scale, 0, 0, setupInfo);

			/* DRAW TIME */

		gGlobalColorFilter = tint;

		snprintf(s, sizeof(s), "%d", (int) (timer+.5f));

		x2 = x + spacing;
		Atlas_DrawString(SPRITE_GROUP_FONT, s, x2, y, fontScale, 0, 0, setupInfo);

		y += lineSpacing;												// move down to prep for next item

		gGlobalColorFilter = noTint;
	}
}


/********************* DRAW TAG TIMER **************************/

static void Infobar_DrawTagTimer(const OGLSetupOutputType *setupInfo)
{
short	p,p2;
float	timer,x,y, scale, spacing;

	x = gIconInfo[ICON_TIMER][gActiveSplitScreenMode].x;
	y = gIconInfo[ICON_TIMER][gActiveSplitScreenMode].y;
	scale = gIconInfo[ICON_TIMER][gActiveSplitScreenMode].scale;
	spacing = gIconInfo[ICON_TIMER][gActiveSplitScreenMode].xSpacing;


			/********************/
			/* DRAW THE TIMEBAR */
			/********************/

				/* DRAW BAR */

	DrawSprite(SPRITE_GROUP_INFOBAR, INFOBAR_SObjType_TimeBar,	x, y, scale, 0, 0, setupInfo);



		/* DRAW THE TIME MARKER */

	x = gIconInfo[ICON_TIMERINDEX][gActiveSplitScreenMode].x;
	y = gIconInfo[ICON_TIMERINDEX][gActiveSplitScreenMode].y;
	scale = gIconInfo[ICON_TIMERINDEX][gActiveSplitScreenMode].scale;
	spacing = gIconInfo[ICON_TIMERINDEX][gActiveSplitScreenMode].xSpacing;


	p = GetPlayerNum(gCurrentSplitScreenPane);
	timer = (gPlayerInfo[p].tagTimer / TAG_TIME_LIMIT);							// get timer value 0..1
	x += timer * spacing;

	DrawSprite(SPRITE_GROUP_INFOBAR, INFOBAR_SObjType_Marker, x, y, scale, 0, 0, setupInfo);


		/**********************************************/
		/* ALSO DRAW TIME MARKER FOR TAGGED OPPONENT */
		/**********************************************/

	if (gWhoIsIt != p)							// only if it isn't us
	{
		p2 = gWhoIsIt;							// in tag, show timer of tagged player

		x = gIconInfo[ICON_TIMERINDEX][gActiveSplitScreenMode].x;
		timer = (gPlayerInfo[p2].tagTimer / TAG_TIME_LIMIT);							// get timer value 0..1
		x += timer * spacing;

		gGlobalColorFilter = gTagColor;							// tint
		gGlobalTransparency = .35;

		DrawSprite(SPRITE_GROUP_INFOBAR, INFOBAR_SObjType_Marker, x, y, scale, 0, 0, setupInfo);

		gGlobalColorFilter.r =
		gGlobalColorFilter.g =
		gGlobalColorFilter.b =
		gGlobalTransparency = 1;
	}


}


/********************* DRAW HEALTH **************************/

static void Infobar_DrawHealth(const OGLSetupOutputType *setupInfo)
{
short	p,p2;
float	timer,x,y, scale, spacing, dist;

	x = gIconInfo[ICON_TIMER][gActiveSplitScreenMode].x;
	y = gIconInfo[ICON_TIMER][gActiveSplitScreenMode].y;
	scale = gIconInfo[ICON_TIMER][gActiveSplitScreenMode].scale;
	spacing = gIconInfo[ICON_TIMER][gActiveSplitScreenMode].xSpacing;


			/********************/
			/* DRAW THE TIMEBAR */
			/********************/

				/* DRAW BAR */

	DrawSprite(SPRITE_GROUP_INFOBAR, INFOBAR_SObjType_TimeBar,	x, y, scale, 0, 0, setupInfo);



		/* DRAW THE TIME MARKER */

	x = gIconInfo[ICON_TIMERINDEX][gActiveSplitScreenMode].x;
	y = gIconInfo[ICON_TIMERINDEX][gActiveSplitScreenMode].y;
	scale = gIconInfo[ICON_TIMERINDEX][gActiveSplitScreenMode].scale;
	spacing = gIconInfo[ICON_TIMERINDEX][gActiveSplitScreenMode].xSpacing;


	p = GetPlayerNum(gCurrentSplitScreenPane);
	timer = gPlayerInfo[p].health;							// get timer value 0..1
	x += timer * spacing;

	DrawSprite(SPRITE_GROUP_INFOBAR, INFOBAR_SObjType_Marker, x, y, scale, 0, 0, setupInfo);


		/**********************************************/
		/* ALSO DRAW TIME MARKER FOR CLOSEST OPPONENT */
		/**********************************************/

	p2 = FindClosestPlayerInFront(gPlayerInfo[p].objNode, 10000, false, &dist, .5);			// check directly ahead
	if (p2 == -1)
		p2 = FindClosestPlayer(gPlayerInfo[p].objNode, gPlayerInfo[p].coord.x, gPlayerInfo[p].coord.z, 20000, false, &dist);

	if (p2 != -1)
	{
		x = gIconInfo[ICON_TIMERINDEX][gActiveSplitScreenMode].x;
		timer = gPlayerInfo[p2].health;
		x += timer * spacing;

		gGlobalColorFilter.r = 1;							// tint
		gGlobalColorFilter.g = 0;
		gGlobalColorFilter.b = 0;
		gGlobalTransparency = .35;

		DrawSprite(SPRITE_GROUP_INFOBAR, INFOBAR_SObjType_Marker, x, y, scale, 0, 0, setupInfo);

		gGlobalColorFilter.r =
		gGlobalColorFilter.g =
		gGlobalColorFilter.b =
		gGlobalTransparency = 1;
	}

}


/********************* DRAW FLAGS **************************/

static void Infobar_DrawFlags(const OGLSetupOutputType *setupInfo)
{
short	i,p,t;
float	x,y, scale, spacing;



	p = GetPlayerNum(gCurrentSplitScreenPane);
	t = gPlayerInfo[p].team;							// get team #

	x 		= gIconInfo[ICON_FIRE][gActiveSplitScreenMode].x;
	y 		= gIconInfo[ICON_FIRE][gActiveSplitScreenMode].y;
	scale 	= gIconInfo[ICON_FIRE][gActiveSplitScreenMode].scale;
	spacing	= gIconInfo[ICON_FIRE][gActiveSplitScreenMode].xSpacing;

	for (i = 0; i < gCapturedFlagCount[t]; i++)
	{
		DrawSprite(SPRITE_GROUP_INFOBAR, INFOBAR_SObjType_RedTorch + t,	x, y, scale, 0, 0, setupInfo);
		x += spacing;
	}


}




#pragma mark -



/****************** SHOW LAPNUM *********************/
//
// Called from Checkpoints.c whenever the player completes a new lap (except for winning lap).
//

void ShowLapNum(short playerNum)
{
short	lapNum;

	if ((!gPlayerInfo[playerNum].onThisMachine) || gPlayerInfo[playerNum].isComputer)
		return;

	lapNum = gPlayerInfo[playerNum].lapNum;


			/* SEE IF TELL LAP */

	if (lapNum > 0)
	{
		PlayAnnouncerSound(EFFECT_LAP2 + lapNum - 1,false, 0);

		NewObjectDefinitionType def =
		{
			.coord		= {0,0,0},
			.moveCall	= MoveLapMessage,
			.scale		= .7f,
			.slot		= SPRITE_SLOT,
		};

		const char* s = Localize(lapNum == 1 ? STR_LAP_2 : STR_LAP_3);
		TextMesh_New(s, kTextMeshAlignCenter, &def);
	}
}

/************* MOVE LAP MESSAGE *****************/

static void MoveLapMessage(ObjNode *theNode)
{
	theNode->ColorFilter.a -= gFramesPerSecondFrac * .5f;
}


/****************** SHOW FINAL PLACE *********************/
//
// Called from Checkpoints.c whenever the player completes a race.
//

void ShowFinalPlace(short playerNum)
{
short	place;

	if ((!gPlayerInfo[playerNum].onThisMachine) || gPlayerInfo[playerNum].isComputer)
		return;

	place = gPlayerInfo[playerNum].place;


			/* MAKE SPRITE OBJECT */

	NewObjectDefinitionType def =
	{
		.group 		= SPRITE_GROUP_INFOBAR,
		.type		= INFOBAR_SObjType_Place1+place,
		.coord		= {0,0,0},
		.flags		= STATUS_BIT_ONLYSHOWTHISPLAYER,
		.slot		= SPRITE_SLOT,
		.moveCall	= MoveFinalPlace,
		.scale		= 1.5,
	};
	gFinalPlaceObj = MakeSpriteObject(&def, gGameViewInfoPtr);

	gFinalPlaceObj->PlayerNum = playerNum;						// only show for this player

	PlayAnnouncerSound(EFFECT_1st + place, true, 1.0);

}


/**************** MOVE FINAL PLACE ***************************/

static void MoveFinalPlace(ObjNode *theNode)
{
	theNode->Rot.z = sin(theNode->SpecialF[0]) * .2f;
	theNode->SpecialF[0] += gFramesPerSecondFrac * 5.0f;
	UpdateObjectTransforms(theNode);
}

#pragma mark -


/********************** DEC CURRENT POW QUANTITY *********************/

void DecCurrentPOWQuantity(short playerNum)
{
	gPlayerInfo[playerNum].powQuantity -= 1;
	if (gPlayerInfo[playerNum].powQuantity <= 0)
		gPlayerInfo[playerNum].powType = POW_TYPE_NONE;
}


#pragma mark -


/****************** SHOW WIN LOSE *********************/
//
// Used for battle modes to show player if they win or lose.
//
// INPUT: 	mode 0 : elimminated
//			mode 1 : won
//			mode 2 : lost
//

void ShowWinLose(short playerNum, Byte mode, short winner)
{
static const float scale[3] =
{
	.8,
	.7,
	.7
};

			/* ONLY DO THIS FOR HUMANS ON THIS MACHINE */

	if ((!gPlayerInfo[playerNum].onThisMachine) || gPlayerInfo[playerNum].isComputer)
		return;


		/* SAY THINGS ONLY IF NETWORK */

	if (gNetGameInProgress)
	{
		switch(mode)
		{
			case	0:
			case	2:
					PlayAnnouncerSound(EFFECT_YOULOSE, true, .5);
					break;

			default:
					PlayAnnouncerSound(EFFECT_YOUWIN, true, .5);
		}
	}


	if (gWinLoseString[playerNum])							// see if delete existing message (usually to change ELIMINATED to xxx WINS
	{
		DeleteObject(gWinLoseString[playerNum]);
		gWinLoseString[playerNum] = nil;
	}


			/* MAKE SPRITE OBJECT */

	NewObjectDefinitionType def =
	{
		.coord		= { 0,0,0 },
		.flags		= STATUS_BIT_ONLYSHOWTHISPLAYER,
		.slot		= SPRITE_SLOT,
		.scale		= scale[gActiveSplitScreenMode],
	};

	switch(mode)
	{
		case	0:
				gWinLoseString[playerNum] = TextMesh_New(Localize(STR_ELIMINATED), 0, &def);
				break;

		case	1:
				gWinLoseString[playerNum] = TextMesh_New(Localize(STR_YOU_WIN), 0, &def);
				break;

		case	2:
				if (gNetGameInProgress && (gGameMode != GAME_MODE_CAPTUREFLAG) && (winner != -1))		// if net game & not CTF, then show name of winner
				{
					char s[64];
					char safePlayerName[32];

					memset(s, 0, sizeof(s));
					memset(safePlayerName, 0, sizeof(safePlayerName));

					int j = 0;
					for (int i = 0; i < 20; i++)									// copy name
					{
						char c = gPlayerNameStrings[winner][i];

						if ((c >= 'a') && (c <= 'z'))								// convert to UPPER CASE
							c = 'A' + (c - 'a');

						if (strchr(PLAYER_NAME_SAFE_CHARSET, c))					// safe characters
							safePlayerName[j++] = c;
					}

					snprintf(s, sizeof(s), "%s %s", safePlayerName, Localize(STR_3RDPERSON_WINS));	// insert "WINS"

					gWinLoseString[playerNum] = TextMesh_New(s, 0, &def);
				}
				else
				{
					gWinLoseString[playerNum] = TextMesh_New(Localize(STR_YOU_LOSE), 0, &def);
				}
				break;

	}


	gWinLoseString[playerNum]->PlayerNum = playerNum;						// only show for this player

}


#pragma mark -

/******************** MAKE TRACK NAME ***************************/

void MakeIntroTrackName(void)
{
ObjNode	*newObj;
static const float scale[3] =
{
	.9,
	.7,
	.7
};

	NewObjectDefinitionType def =
	{
		.coord		= {0,0,0},
		.slot 		= SPRITE_SLOT,
		.moveCall 	= MoveTrackName,
		.scale 	    = scale[gActiveSplitScreenMode],
		.flags		= STATUS_BIT_MOVEINPAUSE,
	};

	newObj = TextMesh_New(Localize(STR_LEVEL_1 + gTrackNum), kTextMeshAlignCenter, &def);

	newObj->ColorFilter.a = 3.5;
}


/******************** MOVE TRACK NAME *************************/

static void MoveTrackName(ObjNode *theNode)
{
	if (gGamePaused)
	{
		theNode->StatusBits |= STATUS_BIT_HIDDEN;
		return;
	}

	theNode->ColorFilter.a -= gFramesPerSecondFrac;
	if (theNode->ColorFilter.a <= 0.0f)
	{
		DeleteObject(theNode);
		return;
	}

	theNode->StatusBits &= ~STATUS_BIT_HIDDEN;
}


/******************** MOVE PRESS ANY KEY TEXT *************************/

static void MovePressAnyKey(ObjNode* theNode)
{
	theNode->SpecialF[0] += gFramesPerSecondFrac * 4.0f;
	theNode->ColorFilter.a = 0.66f + sinf(theNode->SpecialF[0]) * 0.33f;
}

