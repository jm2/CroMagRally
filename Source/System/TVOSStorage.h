#pragma once

// tvOS-only persistent storage bridge.
//
// On tvOS the only app-writable directory (SDL_GetPrefPath resolves to the app's Caches folder)
// is purgeable: the OS can reclaim it at any time, silently wiping the player's prefs, scoreboard,
// and tournament progress. Apps are expected to persist small amounts of state (up to ~1 MB) in
// NSUserDefaults instead. This bridge stores the same [magic][payload] blobs the desktop file path
// writes, keyed by their filename, so File.c's save/load chokepoints can redirect to it on tvOS.
//
// Implemented in TVOSStorage.m (compiled only for the tvOS target).

#ifdef __cplusplus
extern "C" {
#endif

enum
{
	kTVOSStorage_OK = 0,			// success
	kTVOSStorage_NotFound = 1,		// no value stored yet (e.g. first launch)
	kTVOSStorage_Corrupt = 2,		// wrong length or magic mismatch
	kTVOSStorage_Error = 3,			// bad arguments / internal failure
};

int TVOS_SaveUserData(const char* key, const void* magic, long magicLength, const void* payload, long payloadLength);
int TVOS_LoadUserData(const char* key, const void* magic, long magicLength, void* payload, long payloadLength);

#ifdef __cplusplus
}
#endif
