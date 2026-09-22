/****************************/
/*   	CPUFILL.C			*/
/****************************/

#include "game.h"
#include "cpu_fill.h"


/******************** CPU FILL APPLIES TO MODE *********************/

Boolean CPUFillAppliesToMode(int gameMode)
{
	return gameMode == GAME_MODE_MULTIPLAYERRACE;
}


/******************** DECIDE MULTIPLAYER RACE FINISH *********************/
//
// Each human keeps seeing their place among all the cars: a CPU that finishes
// stays ahead of every car still racing (CalcPlayerPlaces).
//

Boolean DecideMultiplayerRaceFinish(const PlayerInfoType players[], short numPlayers, short finisher,
		Boolean raceDecided, Boolean cpuFill, Byte results[])
{
	if (raceDecided || finisher < 0 || finisher >= numPlayers)
		return false;

	if (cpuFill && players[finisher].isComputer)					// CPU finishes don't end a filled race
		return false;

	for (short p = 0; p < numPlayers; p++)
	{
		if (p == finisher)
			results[p] = kRaceResult_Won;
		else if (cpuFill && players[p].isComputer)					// only humans contest a filled race
			results[p] = kRaceResult_None;
		else
			results[p] = kRaceResult_Lost;
	}

	return true;
}
