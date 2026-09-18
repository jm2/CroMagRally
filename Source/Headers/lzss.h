//
// lzss.h

#pragma once

#include <stddef.h>
#include "Pomme.h"

// Return the number of decoded bytes, or -1 for incomplete/invalid input or
// insufficient destination capacity. The caller must discard output on failure.
long LZSS_DecodeBuffer(const void* source, size_t sourceSize, void* destination, size_t capacity);
long LZSS_Decode(short fRefNum, Ptr destPtr, long sourceSize, long capacity);
