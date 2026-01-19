/****************************/
/*      MISC ROUTINES       */
/* (c)1996-2000 Pangea Software  */
/* By Brian Greenstone      */
/****************************/


/***************/
/* EXTERNALS   */
/***************/

#include "game.h"
#include "network.h"
#include <time.h>

extern	SDL_Window* 	gSDLWindow;


/****************************/
/*    CONSTANTS             */
/****************************/

#define	DEFAULT_FPS			9
#define	PTRCOOKIE_SIZE		16

/**********************/
/*     VARIABLES      */
/**********************/

short	gPrefsFolderVRefNum;
long	gPrefsFolderDirID;

uint32_t 	seed0 = 0, seed1 = 0, seed2 = 0;

float	gFramesPerSecond, gFramesPerSecondFrac;

int		gNumPointers = 0;
long	gRAMAlloced = 0;


/**********************/
/*     PROTOTYPES     */
/**********************/



/*********************** DO ALERT *******************/

void DoAlert(const char* format, ...)
{
	Enter2D(true);

	char message[1024];
	va_list args;
	va_start(args, format);
	SDL_vsnprintf(message, sizeof(message), format, args);
	va_end(args);

	SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Game Alert: %s", message);
	SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, GAME_FULL_NAME, message, NULL);

	Exit2D();
}


/*********************** DO FATAL ALERT *******************/

void DoFatalAlert(const char* format, ...)
{
	Enter2D(true);

	char message[1024];
	va_list args;
	va_start(args, format);
	SDL_vsnprintf(message, sizeof(message), format, args);
	va_end(args);

	SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Game Fatal Alert: %s", message);
	SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, GAME_FULL_NAME, message, NULL);//gSDLWindow);

	Exit2D();
	CleanQuit();
}


/************ CLEAN QUIT ***************/

void CleanQuit(void)
{
static Boolean	beenHere = false;

	if (!beenHere)
	{
		DeleteAllObjects();

		beenHere = true;

		SavePlayerFile();								// save player if any

		EndNetworkGame();								// remove me from any active network game
		ShutdownNetworkManager();

		DisposeTerrain();								// dispose of any memory allocated by terrain manager
		DisposeAllBG3DContainers();						// nuke all models
		DisposeCavemanSkins();
		DisposeAllSpriteGroups();						// nuke all sprites
		DisposePillarboxMaterial();

		ShutdownSkeletonManager();

		if (gGameView)							// see if need to dispose this
			OGL_DisposeGameView();

		OGL_Shutdown();									// nuke draw context

		ShutdownSound();								// cleanup sound stuff

		DisposeLocalizedStrings();
	}


	SavePrefs();							// save prefs before bailing

	ExitToShell();
}



#pragma mark -


uint32_t GetRandomSeed(void)
{
	return (uint32_t)gSimRNG.state;
}

/******************** MY RANDOM LONG **********************/
//
// Mapped to SimRandom (Synced)
//
uint32_t MyRandomLong(void)
{
	return SimRandom();
}



/******************** VISUAL RANDOM LONG **********************/
//
// Mapped to LocalRandom (Unsynced)
//
uint32_t VisualRandomLong(void)
{
	return LocalRandom();
}

// Ensure visual seed is somewhat random on startup (Deprecated/Mapped)
void InitVisualRandomSeed(void)
{
	// LocalRNG initialized in LocalRandom init or Main
}



/************************* RANDOM RANGE *************************/
//
// THE RANGE *IS* INCLUSIVE OF MIN AND MAX
//

uint16_t	RandomRange(unsigned short min, unsigned short max)
{
	// 64-bit widen multiply for uniform range
	uint32_t r = SimRandom();
	uint32_t range = max - min + 1;
	return (uint16_t)(((uint64_t)r * range) >> 32) + min;
}




/************** RANDOM FLOAT ********************/
//
// returns a random float between 0 and 1
//

float RandomFloat(void)
{
	// 0x1.0p-24f is 1.0 / 2^24
	return (float)(SimRandom() >> 8) * 0x1.0p-24f;
}


/************** RANDOM FLOAT 2 ********************/
//
// returns a random float between -1 and 1
//

float RandomFloat2(void)
{
	float f = RandomFloat();
	return (f * 2.0f) - 1.0f;
}


/************** VISUAL RANDOM FLOATS ********************/
// Mapped to LocalRandom

float VisualRandomFloat(void)
{
	return (float)(LocalRandom() >> 8) * 0x1.0p-24f;
}

float VisualRandomFloat2(void)
{
	float f = VisualRandomFloat();
	return (f * 2.0f) - 1.0f;
}




/**************** SET MY RANDOM SEED *******************/

void SetMyRandomSeed(unsigned long seed)
{
	InitSimRNG((uint64_t)seed);
}

/**************** INIT MY RANDOM SEED *******************/

void InitMyRandomSeed(void)
{
	InitSimRNG(0x2a80ce30);
}



/**************** POSITIVE MODULO *******************/

int PositiveModulo(int value, unsigned int m)
{
	int mod = value % (int) m;
	if (mod < 0)
	{
		mod += m;
	}
	return mod;
}

#pragma mark -

/****************** ALLOC HANDLE ********************/

Handle	AllocHandle(long size)
{
	Handle hand = NewHandle(size);							// alloc in APPL
	GAME_ASSERT(hand);
	return(hand);
}


/****************** ALLOC PTR ********************/

void *AllocPtr(long size)
{
	GAME_ASSERT(size >= 0);
	GAME_ASSERT(size <= 0x7FFFFFFF);

	size += PTRCOOKIE_SIZE;						// make room for our cookie & whatever else (also keep to 16-byte alignment!)
	Ptr p = SDL_malloc(size);
	GAME_ASSERT(p);

	uint32_t* cookiePtr = (uint32_t *)p;
	cookiePtr[0] = 'FACE';
	cookiePtr[1] = (uint32_t) size;
	cookiePtr[2] = 'PTR3';
	cookiePtr[3] = 'PTR4';

	gNumPointers++;
	gRAMAlloced += size;

	return p + PTRCOOKIE_SIZE;
}


/****************** ALLOC PTR CLEAR ********************/

void *AllocPtrClear(long size)
{
	GAME_ASSERT(size >= 0);
	GAME_ASSERT(size <= 0x7FFFFFFF);

	size += PTRCOOKIE_SIZE;						// make room for our cookie & whatever else (also keep to 16-byte alignment!)
	Ptr p = SDL_calloc(1, size);
	GAME_ASSERT(p);

	uint32_t* cookiePtr = (uint32_t *)p;
	cookiePtr[0] = 'FACE';
	cookiePtr[1] = (uint32_t) size;
	cookiePtr[2] = 'PTC3';
	cookiePtr[3] = 'PTC4';

	gNumPointers++;
	gRAMAlloced += size;

	return p + PTRCOOKIE_SIZE;
}


/****************** REALLOC PTR ********************/

void* ReallocPtr(void* initialPtr, long newSize)
{
	GAME_ASSERT(newSize >= 0);
	GAME_ASSERT(newSize <= 0x7FFFFFFF);

	if (initialPtr == NULL)
	{
		return AllocPtr(newSize);
	}

	Ptr p = ((Ptr)initialPtr) - PTRCOOKIE_SIZE;	// back up pointer to cookie
	newSize += PTRCOOKIE_SIZE;					// make room for our cookie & whatever else (also keep to 16-byte alignment!)

	p = SDL_realloc(p, newSize);				// reallocate it
	GAME_ASSERT(p);

	uint32_t* cookiePtr = (uint32_t *)p;
	GAME_ASSERT(cookiePtr[0] == 'FACE');		// realloc shouldn't have touched our cookie

	uint32_t initialSize = cookiePtr[1];		// update heap size metric
	gRAMAlloced += newSize - initialSize;

	cookiePtr[0] = 'FACE';						// rewrite cookie
	cookiePtr[1] = (uint32_t) newSize;
	cookiePtr[2] = 'REA3';
	cookiePtr[3] = 'REA4';

	return p + PTRCOOKIE_SIZE;
}


/***************** SAFE DISPOSE PTR ***********************/

void SafeDisposePtr(void *ptr)
{
	if (ptr == NULL)
	{
		return;
	}

	Ptr p = ((Ptr)ptr) - PTRCOOKIE_SIZE;			// back up to pt to cookie

	uint32_t* cookiePtr = (uint32_t *)p;

	// Check cookie with better error messages
	if (cookiePtr[0] != 'FACE')
	{
		if (cookiePtr[0] == 'DEAD')
		{
			DoFatalAlert("SafeDisposePtr: DOUBLE FREE detected! Pointer already freed.");
		}
		else
		{
			DoFatalAlert("SafeDisposePtr: INVALID POINTER! Cookie=0x%08X (expected 'FACE'=0x%08X). Not allocated by AllocPtr?",
			             cookiePtr[0], (uint32_t)'FACE');
		}
	}

	gRAMAlloced -= cookiePtr[1];					// deduct ptr size from heap size

	cookiePtr[0] = 'DEAD';							// zap cookie

	SDL_free(p);

	gNumPointers--;
}


#pragma mark -

/******************* VERIFY SYSTEM ******************/

void InitPrefsFolder(bool createIt)
{
OSErr iErr;
long createdDirID;

			/* CHECK PREFERENCES FOLDER */

	iErr = FindFolder(kOnSystemDisk,kPreferencesFolderType,kDontCreateFolder,		// locate the folder
					&gPrefsFolderVRefNum,&gPrefsFolderDirID);
	if (iErr != noErr)
		DoAlert("Warning: Cannot locate the Preferences folder.");

	if (createIt)
	{
		iErr = DirCreate(gPrefsFolderVRefNum, gPrefsFolderDirID, PREFS_FOLDER_NAME, &createdDirID);		// make folder in there
	}
}



#pragma mark -



/************** CALC FRAMES PER SECOND *****************/
//
// This version uses UpTime() which is only available on PCI Macs.
//

void CalcFramesPerSecond(void)
{
static UnsignedWide time;
UnsignedWide currTime;
unsigned long deltaTime;

	Microseconds(&currTime);
	deltaTime = currTime.lo - time.lo;

	gFramesPerSecond = 1000000.0f / deltaTime;

	if (gFramesPerSecond < DEFAULT_FPS)			// (avoid divide by 0's later)
		gFramesPerSecond = DEFAULT_FPS;

#if _DEBUG
	if (GetKeyState(SDL_SCANCODE_KP_PLUS))		// debug speed-up with KP_PLUS
		gFramesPerSecond = 10;
#endif

	gFramesPerSecondFrac = 1.0f/gFramesPerSecond;		// calc fractional for multiplication

	time = currTime;	// reset for next time interval
}


/********************* IS POWER OF 2 ****************************/

Boolean IsPowerOf2(int num)
{
int		i;

	i = 2;
	do
	{
		if (i == num)				// see if this power of 2 matches
			return(true);
		i *= 2;						// next power of 2
	}while(i <= num);				// search until power is > number

	return(false);
}


#pragma mark -

/******************* MY FLUSH EVENTS **********************/
//
// This call makes damed sure that there are no events anywhere in any event queue.
//

void MyFlushEvents(void)
{
#if 0 //IJ
EventRecord 	theEvent;

	FlushEvents (everyEvent, REMOVE_ALL_EVENTS);
	FlushEventQueue(GetMainEventQueue());

			/* POLL EVENT QUEUE TO BE SURE THINGS ARE FLUSHED OUT */

	while (GetNextEvent(mDownMask|mUpMask|keyDownMask|keyUpMask|autoKeyMask, &theEvent));


	FlushEvents (everyEvent, REMOVE_ALL_EVENTS);
	FlushEventQueue(GetMainEventQueue());
#endif
}


/************* SDL_snprintf THAT APPENDS TO EXISTING STRING ****************/

size_t snprintfcat(char* buf, size_t bufSize, char const* fmt, ...)
{
	size_t len = SDL_strnlen(buf, bufSize);
	int result;
	va_list args;

	va_start(args, fmt);
	result = SDL_vsnprintf(buf + len, bufSize - len, fmt, args);
	va_end(args);

	return result;
}



void AdvanceTextCursor(int snprintfReturnCode, char** cursor, size_t* remainingSize)
{
	if (snprintfReturnCode > 0)
	{
		*cursor += snprintfReturnCode;
		*remainingSize -= snprintfReturnCode;
	}
}
