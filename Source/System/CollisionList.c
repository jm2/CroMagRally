/****************************/
/*    COLLISION LIST.C      */
/****************************/


#include "game.h"


/******************* APPEND COLLISION REC *********************/
//
// Reserves the next entry of a fixed-size collision list. Once the list holds
// `capacity` entries it returns nil and leaves the list alone, so callers keep
// the first hits they found and drop the rest instead of writing past the array.
//

CollisionRec* AppendCollisionRec(CollisionRec *list, short *numCollisions, short capacity)
{
	if ((*numCollisions < 0) || (*numCollisions >= capacity))
		return(nil);

	return(&list[(*numCollisions)++]);
}
