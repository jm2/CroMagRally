// tvOS-only persistent storage bridge (see TVOSStorage.h). Stores each save file's
// [magic][payload] blob in NSUserDefaults, keyed by the file's name, because the tvOS Caches
// directory that SDL_GetPrefPath returns is purgeable by the system.

#import <Foundation/Foundation.h>
#include <string.h>
#include "TVOSStorage.h"

int TVOS_SaveUserData(const char* key, const void* magic, long magicLength, const void* payload, long payloadLength)
{
	if (!key || !magic || magicLength < 0 || payloadLength < 0 || (!payload && payloadLength != 0))
		return kTVOSStorage_Error;

	@autoreleasepool
	{
		NSString* defaultsKey = [NSString stringWithUTF8String:key];
		if (!defaultsKey)
			return kTVOSStorage_Error;

		// Store the same layout the desktop file path writes: magic first, then payload.
		NSMutableData* blob = [NSMutableData dataWithCapacity:(NSUInteger)(magicLength + payloadLength)];
		[blob appendBytes:magic length:(NSUInteger)magicLength];
		if (payloadLength > 0)
			[blob appendBytes:payload length:(NSUInteger)payloadLength];

		// NSUserDefaults persists asynchronously on its own; -synchronize is deprecated and not needed.
		[[NSUserDefaults standardUserDefaults] setObject:blob forKey:defaultsKey];
		return kTVOSStorage_OK;
	}
}

int TVOS_LoadUserData(const char* key, const void* magic, long magicLength, void* payload, long payloadLength)
{
	if (!key || !magic || magicLength < 0 || payloadLength < 0 || (!payload && payloadLength != 0))
		return kTVOSStorage_Error;

	@autoreleasepool
	{
		NSString* defaultsKey = [NSString stringWithUTF8String:key];
		if (!defaultsKey)
			return kTVOSStorage_Error;

		NSData* blob = [[NSUserDefaults standardUserDefaults] dataForKey:defaultsKey];
		if (!blob)
			return kTVOSStorage_NotFound;

		// Validate exactly like the file path: total length must match, and the magic must agree.
		if ((long) blob.length != magicLength + payloadLength)
			return kTVOSStorage_Corrupt;
		if (memcmp(blob.bytes, magic, (size_t) magicLength) != 0)
			return kTVOSStorage_Corrupt;

		if (payloadLength > 0)
			memcpy(payload, (const char*) blob.bytes + magicLength, (size_t) payloadLength);

		return kTVOSStorage_OK;
	}
}
