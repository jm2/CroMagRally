/****************************/
/*   	CAR COUNT TUNING.C	    */
/****************************/

#include "car_count_tuning.h"


/******************** GET CATCH-UP PLACE SCALE ***********************/
//
// Fields of TUNED_NUM_CARS or fewer (including 0 and 1) keep the original per-place
// step, so this returns exactly 1.0f for them and callers' products are unchanged.
//

float GetCatchUpPlaceScale(int numCars)
{
	if (numCars <= TUNED_NUM_CARS)
		return 1.0f;

	return (float)(TUNED_NUM_CARS - 1) / (float)(numCars - 1);
}


/******************** GET POW RESPAWN SCALE ***********************/
//
// Uses the whole field, not the cars still racing: it is fixed for the race and
// identical on every peer, so hidden POWs reappear on the same frame everywhere.
//

float GetPOWRespawnScale(int numCars)
{
	if (numCars <= TUNED_NUM_CARS)
		return 1.0f;

	return (float)TUNED_NUM_CARS / (float)numCars;
}


/******************** GET ELIMINATION TAG TIME SCALE ***********************/
//
// Same shape as the catch-up scale: exactly 1.0f up to TUNED_NUM_CARS players, so
// those games keep their allowance to the bit.
//

float GetEliminationTagTimeScale(int numPlayers)
{
	if (numPlayers <= TUNED_NUM_CARS)
		return 1.0f;
	return (float)(TUNED_NUM_CARS - 1) / (float)(numPlayers - 1);
}
