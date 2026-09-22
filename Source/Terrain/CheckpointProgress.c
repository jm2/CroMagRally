/****************************/
/*   CHECKPOINT PROGRESS.C  */
/****************************/


#include "game.h"


/************************* CROSS CHECKPOINT ******************************/
//
// Lap bookkeeping for a player whose movement crossed checkpoint c. It only
// touches the state passed in, so it can be tested without a playfield.
// Returns true if the crossing completed a lap; the caller then calls NextLap.
//
// Backing over the finish line takes the c == 0 path, which clears the tags so
// driving forward over it again doesn't count another lap. The original code
// also had a lap-decrement branch for that case and an all-tags lap check after
// "went back"; neither was reachable, so they're gone.
//

Boolean CrossCheckpoint(short lapNum, short *checkpointNum, Boolean *checkpointTagged, long numCheckpoints, short c)
{
short	oldCheckpoint = *checkpointNum;
Boolean	didLap = false;

			/* SEE IF CROSSED FINISH LINE */
			//
			// This can happen by going forward or backward over it, so
			// we need to handle it carefully.
			//

	if (c == 0)
	{
				/* SEE IF WENT FORWARD THRU FINISH LINE */

		if (oldCheckpoint == (numCheckpoints - 1))
		{
			long	count = 0;

			for (long i = 0; i < numCheckpoints; i++)							// count # of checkpoints tagged
			{
				if (checkpointTagged[i])
					count++;
			}

			didLap = count > (numCheckpoints / 2);								// if crossed at least 50% of the checkpoints then assume we did a full lap
		}

				/* RESET ALL CHECKPOINT TAGS WHENEVER WE CROSS THE FINISH LINE*/

		for (long i = 0; i < numCheckpoints; i++)
			checkpointTagged[i] = false;

		*checkpointNum = c;
	}

			/* SEE IF FORWARD */
	else
	if (c > oldCheckpoint)
	{
		*checkpointNum = c;
		checkpointTagged[c] = true;
	}

			/* STARTED BEHIND THE LAST CHECKPOINT */
			//
			// A grid slot behind checkpoint N-1 crosses it forward before ever reaching
			// the finish line. Every tag is still set from InitPlayersAtStartOfLevel, so
			// keep that state instead of treating it as driving backward, which would
			// untag N-1 and leave the car a lap down.
			//
	else
	if (lapNum < 0 && c == oldCheckpoint && c == numCheckpoints - 1)
	{
		// nothing changes until the finish line starts lap 0
	}

			/* SEE IF WENT BACK */
	else
	{
		*checkpointNum = c - 1;
		checkpointTagged[c] = false;											// untag the other checkpoint
	}

	return didLap;
}
