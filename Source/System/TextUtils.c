#include "game.h"
#include <stdio.h>

void AdvanceTextCursor(int snprintfReturnCode, char** cursor, size_t* remainingSize)
{
	if (snprintfReturnCode > 0 && *remainingSize > 0)
	{
		size_t actualBytesWritten = (size_t) snprintfReturnCode;
		if (actualBytesWritten >= *remainingSize)
			actualBytesWritten = *remainingSize - 1;
		*cursor += actualBytesWritten;
		*remainingSize -= actualBytesWritten;
	}
}

int VFormatTextWithPlaceholder(const char* text, char* buf0, size_t bufSize, const char* format, va_list args)
{
	char* buf = buf0;
	if (bufSize == 0)
		return 0;
	buf[0] = '\0';

	const char* placeholder = strchr(text, '#');
	if (!placeholder)
	{
		int rc = snprintf(buf, bufSize, "%s", text);
		return rc < 0 ? 0 : (int) GAME_MIN((size_t) rc, bufSize - 1);
	}

	size_t prefixBytes = GAME_MIN((size_t) (placeholder - text), bufSize - 1);
	memcpy(buf, text, prefixBytes);
	buf[prefixBytes] = '\0';
	buf += prefixBytes;
	bufSize -= prefixBytes;

	int rc = vsnprintf(buf, bufSize, format, args);
	if (rc < 0)
		return (int) (buf - buf0);
	AdvanceTextCursor(rc, &buf, &bufSize);

	rc = snprintf(buf, bufSize, "%s", placeholder + 1);
	AdvanceTextCursor(rc, &buf, &bufSize);
	return (int) (buf - buf0);
}
