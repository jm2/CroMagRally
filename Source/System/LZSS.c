/****************************/
/*        LZSS.C            */
/* (c)2000 Pangea Software  */
/* By Brian Greenstone      */
/****************************/

#include "game.h"
#include "lzss.h"

long LZSS_Decode(short fRefNum, Ptr destPtr, long sourceSize, long capacity)
{
	if (sourceSize < 0 || capacity < 0 || (!destPtr && capacity != 0))
		return -1;
	if (sourceSize == 0)
		return 0;

	Ptr source = AllocPtr(sourceSize);
	long count = sourceSize;
	long decodedSize = -1;
	if (FSRead(fRefNum, &count, source) == noErr && count == sourceSize)
		decodedSize = LZSS_DecodeBuffer(source, sourceSize, destPtr, capacity);
	SafeDisposePtr(source);
	return decodedSize;
}
