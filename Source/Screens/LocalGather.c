/****************************/
/* LOCAL GATHER.C           */
/* (C) 2022 Iliyas Jorio    */
/****************************/


/****************************/
/*    EXTERNALS             */
/****************************/

#include "game.h"
#include "miscscreens.h"
#include <SDL.h>


/****************************/
/*    PROTOTYPES            */
/****************************/

static void SetupLocalGatherScreen(void);
static int DoLocalGatherControls(void);



/****************************/
/*    CONSTANTS             */
/****************************/

/*********************/
/*    VARIABLES      */
/*********************/

static ObjNode* gGatherPrompt = NULL;
static int gNumControllersMissing = 4;




static void UpdateGatherPrompt(int numControllersMissing)
{
	char message[256];

	if (numControllersMissing <= 0)
	{
		TextMesh_Update("OK", 0, gGatherPrompt);
		gGatherPrompt->Scale.x = 1;
		gGatherPrompt->Scale.y = 1;
		UpdateObjectTransforms(gGatherPrompt);
		gGameView->fadeOutDuration = .3f;
	}
	else
	{
		snprintf(
			message,
			sizeof(message),
			"%s %s\n%s",
			Localize(STR_CONNECT_CONTROLLERS_PREFIX),
			Localize(STR_CONNECT_1_CONTROLLER + numControllersMissing - 1),
			Localize(numControllersMissing==1? STR_CONNECT_CONTROLLERS_SUFFIX_KBD: STR_CONNECT_CONTROLLERS_SUFFIX));

		TextMesh_Update(message, 0, gGatherPrompt);
	}

}




/********************** DO GATHER SCREEN **************************/
//
// Return true if user aborts.
//

Boolean DoLocalGatherScreen(void)
{
	gNumControllersMissing = gNumLocalPlayers;
	UnlockPlayerControllerMapping();

	if (GetNumControllers() >= gNumLocalPlayers)
	{
		// Skip gather screen if we already have enough controllers
		return false;
	}

	SetupLocalGatherScreen();
	MakeFadeEvent(true);


				/*************/
				/* MAIN LOOP */
				/*************/

	CalcFramesPerSecond();
	ReadKeyboard();

	int outcome = 0;

	while (outcome == 0)
	{
			/* SEE IF MAKE SELECTION */

		outcome = DoLocalGatherControls();


		int numControllers = GetNumControllers();
		
		gNumControllersMissing = gNumLocalPlayers - numControllers;
		if (gNumControllersMissing < 0)
			gNumControllersMissing = 0;

		UpdateGatherPrompt(gNumControllersMissing);


			/**************/
			/* DRAW STUFF */
			/**************/

		CalcFramesPerSecond();
		ReadKeyboard();
		MoveObjects();
		OGL_DrawScene(DrawObjects);
	}

			/* SHOW 'OK!' */

	if (outcome >= 0)
	{
		UpdateGatherPrompt(0);
	}


			/***********/
			/* CLEANUP */
			/***********/

	OGL_FadeOutScene(DrawObjects, MoveObjects);

	DeleteAllObjects();
	FreeAllSkeletonFiles(-1);
	DisposeAllBG3DContainers();
	OGL_DisposeGameView();


		/* SET CHARACTER TYPE SELECTED */

	return (outcome < 0);
}


/********************* SETUP LOCAL GATHER SCREEN **********************/

static void SetupLocalGatherScreen(void)
{
	SetupGenericMenuScreen(true);


			/*****************/
			/* BUILD OBJECTS */
			/*****************/

	NewObjectDefinitionType def2 =
	{
		.scale = 0.4f,
		.coord = {0, 0, 0},
		.slot = SPRITE_SLOT,
	};

	gGatherPrompt = TextMesh_NewEmpty(256, &def2);
}



/***************** DO CHARACTERSELECT CONTROLS *******************/

static int DoLocalGatherControls(void)
{
	if (gNumControllersMissing == 0)
		return 1;

	if (GetNewNeedStateAnyP(kNeed_UIBack))
	{
		return -1;
	}

		/* SEE IF SELECT THIS ONE */

	if (GetNewKeyState(SDL_SCANCODE_RETURN) || GetNewKeyState(SDL_SCANCODE_KP_ENTER))
	{
		// User pressed [ENTER] on keyboard
		if (gNumControllersMissing == 1)
		{
			PlayEffect_Parms(EFFECT_SELECTCLICK, FULL_CHANNEL_VOLUME, FULL_CHANNEL_VOLUME, NORMAL_CHANNEL_RATE * 2/3);
			return 1;
		}
		else
		{
			PlayEffect(EFFECT_BADSELECT);
			MakeTwitch(gGatherPrompt, kTwitchPreset_PadlockWiggle);
		}
	}
	else if (GetNewNeedStateAnyP(kNeed_UIConfirm))
	{
		// User pressed [A] on gamepad
		if (gNumControllersMissing > 0)
		{
			PlayEffect(EFFECT_BADSELECT);
			MakeTwitch(gGatherPrompt, kTwitchPreset_PadlockWiggle);
		}
	}
	else if (IsCheatKeyComboDown())		// useful to test local multiplayer without having all controllers plugged in
	{
		PlayEffect(EFFECT_ROMANCANDLE_LAUNCH);
		return 1;
	}

	return 0;
}


