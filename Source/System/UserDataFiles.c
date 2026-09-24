// User-data file I/O and migration from legacy tvOS cache files.
#include "game.h"
#include <limits.h>
#if defined(CMR_USE_TVOS_STORAGE)
#include "TVOSStorage.h"
#endif

static OSErr MakeFSSpecForUserDataFile(const char* filename, FSSpec* spec)
{
	char path[256];
	SDL_snprintf(path, sizeof(path), ":%s:%s", PREFS_FOLDER_NAME, filename);

	return FSMakeFSSpec(gPrefsFolderVRefNum, gPrefsFolderDirID, path, spec);
}

/********* LOAD STRUCT FROM USER FILE IN PREFS FOLDER ***********/

OSErr LoadUserDataFile(const char* filename, const char* magic, long payloadLength, Ptr payloadPtr)
{
OSErr		iErr;
short		refNum;
FSSpec		file;
long		count;
long		eof = 0;
char		fileMagic[64];

	if (!filename || !magic || payloadLength < 0 || (!payloadPtr && payloadLength != 0))
		return paramErr;

	long magicLength = (long) SDL_strlen(magic) + 1;		// including null-terminator
	if (magicLength >= (long) sizeof(fileMagic) || payloadLength > LONG_MAX - magicLength)
		return paramErr;

#if defined(CMR_USE_TVOS_STORAGE)
	switch (TVOS_LoadUserData(filename, magic, magicLength, payloadPtr, payloadLength))
	{
		case kTVOSStorage_OK:			return noErr;
		case kTVOSStorage_NotFound:		break; // Upgrade: try the legacy cache file below.
		case kTVOSStorage_Corrupt:		return badFileFormat;
		default:						return ioErr;
	}
#endif

	iErr = InitPrefsFolder(false);
	if (iErr != noErr)
		return iErr;


				/*************/
				/* READ FILE */
				/*************/

	MakeFSSpecForUserDataFile(filename, &file);
	iErr = FSpOpenDF(&file, fsRdPerm, &refNum);
	if (iErr)
		return iErr;

				/* CHECK FILE LENGTH */

	iErr = GetEOF(refNum, &eof);

	if (iErr != noErr || eof != magicLength + payloadLength)
		goto fileIsCorrupt;

				/* READ HEADER */

	count = magicLength;
	iErr = FSRead(refNum, &count, fileMagic);
	if (iErr ||
		count != magicLength ||
		0 != SDL_memcmp(magic, fileMagic, magicLength))
	{
		goto fileIsCorrupt;
	}

				/* READ PAYLOAD */

	count = payloadLength;
	iErr = FSRead(refNum, &count, payloadPtr);
	if (iErr || count != payloadLength)
	{
		goto fileIsCorrupt;
	}

	FSClose(refNum);
#if defined(CMR_USE_TVOS_STORAGE)
	// Keep the legacy file: defaults persistence is asynchronous, so there is no
	// durable-write acknowledgement that would make deleting the recovery copy safe.
	if (TVOS_SaveUserData(filename, magic, magicLength, payloadPtr, payloadLength) != kTVOSStorage_OK)
		SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Could not migrate '%s'; retaining legacy data for retry", filename);
#endif
	return noErr;

fileIsCorrupt:
	SDL_Log("File '%s' appears to be corrupt!", file.cName);
	FSClose(refNum);
	return badFileFormat;
}


/********* UPGRADE V1 PREFS ***********/
//
// Keeps every v1 setting. The fields v2 added start from their defaults.
//

_Static_assert(sizeof(((PrefsType*)0)->bindings) == sizeof(((PrefsTypeV1*)0)->bindings), "v1 bindings copy whole");
_Static_assert(sizeof(((PrefsType*)0)->playerName) == sizeof(((PrefsTypeV1*)0)->playerName), "v1 name copies whole");

static void UpgradePrefsV1(PrefsType* prefs, const PrefsTypeV1* v1, const PrefsType* defaults)
{
	SDL_memcpy(prefs, defaults, sizeof(*prefs));

	prefs->difficulty			= v1->difficulty;
	prefs->splitScreenMode2P	= v1->splitScreenMode2P;
	prefs->splitScreenMode3P	= v1->splitScreenMode3P;
	prefs->language				= v1->language;
	prefs->tagDuration			= v1->tagDuration;
	prefs->antialiasingLevel	= v1->antialiasingLevel;
	prefs->fullscreen			= v1->fullscreen;
	prefs->displayNumMinus1		= v1->displayNumMinus1;
	prefs->musicVolumePercent	= v1->musicVolumePercent;
	prefs->sfxVolumePercent		= v1->sfxVolumePercent;
	prefs->raceTimer			= v1->raceTimer;
	SDL_memcpy(prefs->bindings, v1->bindings, sizeof(prefs->bindings));
	prefs->gamepadRumble		= v1->gamepadRumble;
	prefs->tournamentProgression = v1->tournamentProgression;
	SDL_memcpy(prefs->playerName, v1->playerName, sizeof(prefs->playerName));
}


/********* LOAD PREFS, UPGRADING OLDER LAYOUTS ***********/
//
// Loads the current prefs layout, or else a v1 file upgraded to it, so that a new
// build keeps the settings an older one saved. *upgraded tells the caller to write
// the file back in the current layout. If neither layout loads, returns the current
// layout's error (e.g. fnfErr on first launch) and leaves prefs undefined.
//

OSErr LoadPrefsFile(const char* filename, PrefsType* prefs, const PrefsType* defaults, Boolean* upgraded)
{
	if (!prefs || !defaults || !upgraded)
		return paramErr;

	*upgraded = false;

	OSErr err = LoadUserDataFile(filename, PREFS_MAGIC, sizeof(*prefs), (Ptr) prefs);
	if (err == noErr)
		return noErr;

	PrefsTypeV1 v1;
	if (LoadUserDataFile(filename, PREFS_MAGIC_V1, sizeof(v1), (Ptr) &v1) != noErr)
		return err;

	UpgradePrefsV1(prefs, &v1, defaults);
	*upgraded = true;
	SDL_Log("Upgraded '%s' from the v1 prefs layout", filename);
	return noErr;
}


/********* SAVE STRUCT TO USER FILE IN PREFS FOLDER ***********/

OSErr SaveUserDataFile(const char* filename, const char* magic, long payloadLength, Ptr payloadPtr)
{
FSSpec				file;
FSSpec				tempFile;
OSErr				iErr;
short				refNum;
long				count;
char				tempFilename[256];

	if (!filename || !magic || payloadLength < 0 || (!payloadPtr && payloadLength != 0))
		return paramErr;

#if defined(CMR_USE_TVOS_STORAGE)
	{
		long magicLength = (long) SDL_strlen(magic) + 1;
		return (TVOS_SaveUserData(filename, magic, magicLength, payloadPtr, payloadLength) == kTVOSStorage_OK)
			? noErr : ioErr;
	}
#endif

	iErr = InitPrefsFolder(true);
	if (iErr != noErr)
		return iErr;

	int tempFilenameLength = SDL_snprintf(tempFilename, sizeof(tempFilename), "%s.tmp", filename);
	if (tempFilenameLength < 0 || tempFilenameLength >= (int) sizeof(tempFilename))
		return bdNamErr;

				/* CREATE A TEMPORARY FILE BESIDE THE DESTINATION */

	MakeFSSpecForUserDataFile(filename, &file);
	MakeFSSpecForUserDataFile(tempFilename, &tempFile);
	FSpDelete(&tempFile);												// discard a stale temp file from an interrupted save
	iErr = FSpCreate(&tempFile, 'CavM', 'Pref', smSystemScript);
	if (iErr)
	{
		return iErr;
	}

				/* OPEN FILE */

	iErr = FSpOpenDF(&tempFile, fsRdWrPerm, &refNum);
	if (iErr)
	{
		FSpDelete(&tempFile);
		return iErr;
	}

				/* WRITE MAGIC */

	long magicLength = (long) SDL_strlen(magic) + 1;
	count = magicLength;
	iErr = FSWrite(refNum, &count, (Ptr) magic);
	if (iErr || count != magicLength)
	{
		FSClose(refNum);
		FSpDelete(&tempFile);
		return iErr != noErr ? iErr : ioErr;
	}

				/* WRITE DATA */

	count = payloadLength;
	iErr = FSWrite(refNum, &count, payloadPtr);
	if (iErr == noErr && count != payloadLength)
		iErr = ioErr;

	OSErr closeErr = FSClose(refNum);
	if (iErr == noErr)
		iErr = closeErr;

	if (iErr != noErr)
	{
		FSpDelete(&tempFile);
		return iErr;
	}

				/* ATOMICALLY PUBLISH THE COMPLETE FILE */

	iErr = FSpAtomicReplace(&tempFile, &file);
	if (iErr != noErr)
	{
		FSpDelete(&tempFile);
		return iErr;
	}

	SDL_Log("Wrote %s", file.cName);

	return noErr;
}
