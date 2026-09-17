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


