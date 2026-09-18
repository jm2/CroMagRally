#include "lzss.h"
#include <limits.h>
#include <string.h>

#define RING_BUFF_SIZE 4096
#define MAX_MATCH_LENGTH 18
#define MIN_MATCH_LENGTH 3

long LZSS_DecodeBuffer(const void* source, size_t sourceSize, void* destination, size_t capacity)
{
	if ((!source && sourceSize) || (!destination && capacity) || capacity > LONG_MAX)
		return -1;
	const unsigned char* input = source;
	unsigned char* output = destination;
	unsigned char ring[RING_BUFF_SIZE];
	// Define the whole initial dictionary, including the write cursor's tail.
	memset(ring, ' ', sizeof(ring));
	size_t in = 0, out = 0;
	unsigned int flags = 0;
	unsigned int cursor = RING_BUFF_SIZE - MAX_MATCH_LENGTH;
	while (in < sourceSize)
	{
		if (((flags >>= 1) & 256) == 0)
		{
			flags = input[in++] | 0xff00;
			if (in == sourceSize)
				return -1;		// flag byte without even one complete token
		}
		if (flags & 1)
		{
			if (out == capacity)
				return -1;
			unsigned char c = input[in++];
			output[out++] = ring[cursor] = c;
			cursor = (cursor + 1) & (RING_BUFF_SIZE - 1);
		}
		else
		{
			if (sourceSize - in < 2)
				return -1;
			unsigned int offset = input[in++];
			unsigned int length = input[in++];
			offset |= (length & 0xf0) << 4;
			length = (length & 0x0f) + MIN_MATCH_LENGTH;
			if (length > capacity - out)
				return -1;
			for (unsigned int k = 0; k < length; k++)
			{
				unsigned char c = ring[(offset + k) & (RING_BUFF_SIZE - 1)];
				output[out++] = ring[cursor] = c;
				cursor = (cursor + 1) & (RING_BUFF_SIZE - 1);
			}
		}
	}
	return (long) out;
}
