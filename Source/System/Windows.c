/****************************/
/*        WINDOWS           */
/* (c)2001 Pangea Software  */
/* By Brian Greenstone      */
/****************************/


/***************/
/* EXTERNALS   */
/***************/

#include 	<stdlib.h>
#include	"globals.h"
#include	"window.h"
#include	"misc.h"
#include "objects.h"
#include "file.h"
#include "input.h"
#include "sound2.h"


extern	NewObjectDefinitionType	gNewObjectDefinition;
extern	ObjNode	*gCurrentNode,*gFirstNodePtr;
extern	float	gFramesPerSecondFrac;
extern	short	gPrefsFolderVRefNum;
extern	long	gPrefsFolderDirID;
extern	Boolean				gOSX, gISpActive;
extern	PrefsType			gGamePrefs;
extern	Boolean			gSongPlayingFlag,gMuteMusicFlag;
extern	AGLContext		gAGLContext;
extern	AGLDrawable		gAGLWin;

/****************************/
/*    PROTOTYPES            */
/****************************/

static void MoveFadeEvent(ObjNode *theNode);

/****************************/
/*    CONSTANTS             */
/****************************/



#if 0
#define MONITORS_TO_FADE	gDisplayContext
#else
#define MONITORS_TO_FADE	nil
#endif


typedef struct
{
	int		rezH,rezV;
	double	lowestHz;
}VideoModeType;


#define	MAX_VIDEO_MODES		30


/**********************/
/*     VARIABLES      */
/**********************/

static	Boolean	gOldISpFlag;

static	Boolean			gOldMuteMusic;

static GDHandle				gOurDevice = nil;


static short				gNumVideoModes = 0;
static VideoModeType		gVideoModeList[MAX_VIDEO_MODES];

long					gScreenXOffset,gScreenYOffset;
WindowPtr				gCoverWindow = nil;
void* /*DSpContextReference*/ 	gDisplayContext = nil;
Boolean					gLoadedDrawSprocket = false;

static unsigned long	gVideoModeTimoutCounter;
static Boolean			gVideoModeTimedOut;


float		gGammaFadePercent;

int				gGameWindowWidth, gGameWindowHeight;

WindowPtr			g1PixelWindow = nil;

CGrafPtr				gDisplayContextGrafPtr = nil;

Boolean				gHasFixedDSpForX = false;

short			g2DStackDepth = 0;


/****************  INIT WINDOW STUFF *******************/

void InitWindowStuff(void)
{
GDHandle 		phGD;
OSErr		iErr;
Rect			r;
float		w,h;



				/****************************/
				/* INIT WITH DRAW SPROCKETS */
				/****************************/

#if USE_DSP
	PrepDrawSprockets();

	w = gGamePrefs.screenWidth;
	h = gGamePrefs.screenHeight;

	DSpContext_GetDisplayID(gDisplayContext, &displayID);				// get context display ID
	iErr = DMGetGDeviceByDisplayID(displayID, &phGD, true);				// get GDHandle for ID'd device
	if (iErr != noErr)
	{
		DoFatalAlert("InitWindowStuff: DMGetGDeviceByDisplayID failed!");
	}

	r.top  		= (short) ((**phGD).gdRect.top + ((**phGD).gdRect.bottom - (**phGD).gdRect.top) / 2);	  	// h center
	r.top  		-= (short) (h / 2);
	r.left  	= (short) ((**phGD).gdRect.left + ((**phGD).gdRect.right - (**phGD).gdRect.left) / 2);		// v center
	r.left  	-= (short) (w / 2);
	r.right 	= (short) (r.left + w);
	r.bottom 	= (short) (r.top + h);


			/* GET SOME INFO ABOUT THIS GDEVICE */

//	if (OGL_CheckRenderer(phGD, &totalVRAM) == false)
//		DoAlert("No 3D hardware acceleration was found.  Check that the drivers for your 3D accelerator are installed properly");


#else
				/********************/
				/* INIT SOME WINDOW */
				/********************/
	
#if 0
	{
		WindowPtr	window;

		SetRect(&r, 100,100,1024,768);

		window = NewCWindow(nil, &r, "", true, plainDBox, (void *)-1, false, nil);
		gDisplayContextGrafPtr = GetWindowPort(window);
	}
#endif

#endif


	gGameWindowWidth = r.right - r.left;
	gGameWindowHeight = r.bottom - r.top;
}


/************************** CHANGE WINDOW SCALE ***********************/
//
// Called whenever gGameWindowShrink is changed.  This updates
// the view and everything else to accomodate new window size.
//

void ChangeWindowScale(void)
{
	IMPLEMENT_ME_SOFT();
#if 0
Rect			r;
GDHandle 		phGD;
DisplayIDType displayID;
float		w,h;


	if ((!gLoadedDrawSprocket) || (gCoverWindow == nil))
		return;

	w = gGamePrefs.screenWidth;
	h = gGamePrefs.screenHeight;

			/* CALC NEW INFO */

	DSpContext_GetDisplayID(gDisplayContext, &displayID);			// get context display ID
	DMGetGDeviceByDisplayID(displayID, &phGD, false);				// get GDHandle for ID'd device

	r.top  		= (short) ((**phGD).gdRect.top + ((**phGD).gdRect.bottom - (**phGD).gdRect.top) / 2);	  	// h center
	r.top  		-= (short) (h / 2);
	r.left  	= (short) ((**phGD).gdRect.left + ((**phGD).gdRect.right - (**phGD).gdRect.left) / 2);		// v center
	r.left  	-= (short) (w / 2);
	r.right 	= (short) (r.left + w);
	r.bottom 	= (short) (r.top + h);


	gGameWindowWidth = r.right - r.left;
	gGameWindowHeight = r.bottom - r.top;


			/* MODIFY THE WINODW TO THE NEW PARMS */

	HideWindow(gCoverWindow);
	MoveWindow(gCoverWindow, r.left, r.top, true);
	SizeWindow(gCoverWindow, gGameWindowWidth, gGameWindowHeight, false);
	ShowWindow(gCoverWindow);
#endif
}



#pragma mark -


/**************** GAMMA FADE IN *************************/

void GammaFadeIn(void)
{
	if (gDisplayContext)
	{
		while(gGammaFadePercent < 100.0f)
		{
			gGammaFadePercent += 7.0f;
			if (gGammaFadePercent > 100.0f)
				gGammaFadePercent = 100.0f;

#if ALLOW_FADE
			DSpContext_FadeGamma(gDisplayContext,gGammaFadePercent,nil);
#endif

			Wait(1);

		}
	}
}

/**************** GAMMA FADE OUT *************************/

void GammaFadeOut(void)
{
	if (gDisplayContext)
	{
		while(gGammaFadePercent > 0.0f)
		{
			gGammaFadePercent -= 7.0f;
			if (gGammaFadePercent < 0.0f)
				gGammaFadePercent = 0.0f;

#if ALLOW_FADE
			DSpContext_FadeGamma(gDisplayContext,gGammaFadePercent,nil);
#endif

			Wait(1);

		}
	}

}

/********************** GAMMA ON *********************/

void GammaOn(void)
{

	if (gDisplayContext)
	{
		if (gGammaFadePercent != 100.0f)
		{
#if ALLOW_FADE
			DSpContext_FadeGamma(MONITORS_TO_FADE,100,nil);
#endif
			gGammaFadePercent = 100.0f;
		}
	}
}


/****************** CLEANUP DISPLAY *************************/

void CleanupDisplay(void)
{
	IMPLEMENT_ME_SOFT();
#if 0
OSStatus 		theError;

	if(gDisplayContext != nil)
	{
#if ALLOW_FADE
		GammaFadeOut();
#endif


		DSpContext_SetState( gDisplayContext, kDSpContextState_Inactive );	// deactivate


#if ALLOW_FADE
		DSpContext_FadeGamma(MONITORS_TO_FADE,100,nil);						// gamme on all
#endif
		DSpContext_Release( gDisplayContext );								// release

		gDisplayContext = nil;
	}


	/* shutdown draw sprocket */

	if (gLoadedDrawSprocket)
	{
		theError = DSpShutdown();
		gLoadedDrawSprocket = false;
	}

	gDisplayContextGrafPtr = nil;
#endif
}

/******************** MAKE FADE EVENT *********************/
//
// INPUT:	fadeIn = true if want fade IN, otherwise fade OUT.
//

void MakeFadeEvent(Boolean	fadeIn)
{
ObjNode	*newObj;
ObjNode		*thisNodePtr;

		/* SCAN FOR OLD FADE EVENTS STILL IN LIST */

	thisNodePtr = gFirstNodePtr;

	while (thisNodePtr)
	{
		if (thisNodePtr->MoveCall == MoveFadeEvent)
		{
			thisNodePtr->Flag[0] = fadeIn;								// set new mode
			return;
		}
		thisNodePtr = thisNodePtr->NextNode;							// next node
	}


		/* MAKE NEW FADE EVENT */

	gNewObjectDefinition.genre = EVENT_GENRE;
	gNewObjectDefinition.flags = 0;
	gNewObjectDefinition.slot = SLOT_OF_DUMB + 1000;
	gNewObjectDefinition.moveCall = MoveFadeEvent;
	newObj = MakeNewObject(&gNewObjectDefinition);

	newObj->Flag[0] = fadeIn;
}


/***************** MOVE FADE EVENT ********************/

static void MoveFadeEvent(ObjNode *theNode)
{
float	fps = gFramesPerSecondFrac;

			/* SEE IF FADE IN */

	if (theNode->Flag[0])
	{
		gGammaFadePercent += 400.0f*fps;
		if (gGammaFadePercent >= 100.0f)										// see if @ 100%
		{
			gGammaFadePercent = 100.0f;
			DeleteObject(theNode);
		}
#if ALLOW_FADE
		if (gDisplayContext)
			DSpContext_FadeGamma(gDisplayContext,gGammaFadePercent,nil);
#endif
	}

			/* FADE OUT */
	else
	{
		gGammaFadePercent -= 400.0f*fps;
		if (gGammaFadePercent <= 0.0f)													// see if @ 0%
		{
			gGammaFadePercent = 0;
			DeleteObject(theNode);
		}
#if ALLOW_FADE
		if (gDisplayContext)
			DSpContext_FadeGamma(gDisplayContext,gGammaFadePercent,nil);
#endif
	}
}


/************************ GAME SCREEN TO BLACK ************************/

void GameScreenToBlack(void)
{
Rect	r;

	if (!gDisplayContextGrafPtr)
		return;

	SetPort(gDisplayContextGrafPtr);
	BackColor(blackColor);

	GetPortBounds(gDisplayContextGrafPtr, &r);
	EraseRect(&r);
}


/************************** ENTER 2D *************************/
//
// For OS X - turn off DSp when showing 2D
//

void Enter2D(Boolean pauseDSp)
{
	IMPLEMENT_ME_SOFT();
#if 0
	InitCursor();
	MyFlushEvents();

	g2DStackDepth++;
	if (g2DStackDepth > 1)						// see if already in 2D
		return;

	gOldISpFlag = gISpActive;					// remember if ISp was on
	TurnOffISp();

	if (!gLoadedDrawSprocket)
		return;
	if (!gDisplayContext)
		return;

	if (gAGLContext)
	{
		glFlush();
		glFinish();
		aglSetDrawable(gAGLContext, nil);		// diable gl for dialog
		glFlush();
		glFinish();
	}



	if (pauseDSp)
	{
		DSpContext_SetState(gDisplayContext, kDSpContextState_Paused);
		gDisplayContextGrafPtr = nil;
	}
#endif
}


/************************** EXIT 2D *************************/
//
// For OS X - turn ON DSp when NOT 2D
//

void Exit2D(void)
{
	IMPLEMENT_ME_SOFT();
#if 0
AGLContext agl_ctx = gAGLContext;

	g2DStackDepth--;
	if (g2DStackDepth > 0)			// don't exit unless on final exit
		return;

	if (gOldISpFlag)
		TurnOnISp();									// resume input sprockets if needed
	HideCursor();


	if (!gLoadedDrawSprocket)
		return;
	if (!gDisplayContext)
		return;


	ReadKeyboard_Real();			// do this to flush the keymap


	if (gDisplayContextGrafPtr == nil)
	{
		DSpContext_SetState(gDisplayContext, kDSpContextState_Active);
		DSpContext_GetFrontBuffer(gDisplayContext, &gDisplayContextGrafPtr);		// get this again since may have changed
	}

	if (gAGLContext)
	{
		gAGLWin = (AGLDrawable)gDisplayContextGrafPtr;
		if (gAGLWin)
		{
			GLboolean      ok = aglSetDrawable(gAGLContext, gAGLWin);					// reenable gl
			if ((!ok) || (aglGetError() != AGL_NO_ERROR))
				DoFatalAlert("Exit2D: aglSetDrawable failed!");

		}
	}
#endif
}


#pragma mark -

/*********************** DUMP GWORLD 2 **********************/
//
//    copies to a destination RECT
//

void DumpGWorld2(GWorldPtr thisWorld, WindowPtr thisWindow,Rect *destRect)
{
	IMPLEMENT_ME_SOFT();
#if 0
PixMapHandle pm;
GDHandle		oldGD;
GWorldPtr		oldGW;
Rect			r;

	DoLockPixels(thisWorld);

	GetGWorld (&oldGW,&oldGD);
	pm = GetGWorldPixMap(thisWorld);
	if ((pm == nil) | (*pm == nil) )
		DoAlert("PixMap Handle or Ptr = Null?!");

	SetPort(GetWindowPort(thisWindow));

	ForeColor(blackColor);
	BackColor(whiteColor);

	GetPortBounds(thisWorld, &r);

	CopyBits((BitMap *)*pm, GetPortBitMapForCopyBits(GetWindowPort(thisWindow)),
			 &r,
			 destRect,
			 srcCopy, 0);

	SetGWorld(oldGW,oldGD);								// restore gworld
#endif
}


/******************* DO LOCK PIXELS **************/

void DoLockPixels(GWorldPtr world)
{
PixMapHandle pm;

	pm = GetGWorldPixMap(world);
	if (LockPixels(pm) == false)
		DoFatalAlert("PixMap Went Bye,Bye?!");
}


/********************** WAIT **********************/

void Wait(long ticks)
{
long	start;

	start = TickCount();

	while (TickCount()-start < ticks);

}

