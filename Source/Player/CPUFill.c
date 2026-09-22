/****************************/
/*   	CPUFILL.C			*/
/****************************/

#include "game.h"
#include "cpu_fill.h"

#define	NUM_DRIVER_LOOKS	(2 * NUM_CAVEMAN_SKINS)				// both sexes in every skin

_Static_assert(NUM_DRIVER_LOOKS <= 32, "a uint32_t holds one bit per driver look");


/******************** CPU FILL APPLIES TO MODE *********************/

Boolean CPUFillAppliesToMode(int gameMode)
{
	return gameMode == GAME_MODE_MULTIPLAYERRACE;
}


/******************** COUNT PLAYERS IN GAME *********************/

short CountPlayersInGame(int gameMode, short numRealPlayers, Boolean cpuFill)
{
	switch (gameMode)
	{
		case	GAME_MODE_PRACTICE:
		case	GAME_MODE_TOURNAMENT:
				return MAX_PLAYERS;

		default:
				if (cpuFill && CPUFillAppliesToMode(gameMode))
					return MAX_PLAYERS;
				return numRealPlayers;
	}
}


/******************** MAKE CPU LOOKS DISTINCT *********************/

static int GetDriverLook(const PlayerInfoType* player)
{
	if (player->sex < 0 || player->sex > 1 || player->skin < 0 || player->skin >= NUM_CAVEMAN_SKINS)
		return -1;
	return player->sex * NUM_CAVEMAN_SKINS + player->skin;
}

void MakeCPULooksDistinct(PlayerInfoType players[], short numPlayers)
{
uint32_t	taken = 0;
uint32_t	needNewLook = 0;

	for (short p = 0; p < numPlayers; p++)							// humans wear what they picked
	{
		int look = GetDriverLook(&players[p]);
		if (!players[p].isComputer && look >= 0)
			taken |= 1u << look;
	}

	for (short p = 0; p < numPlayers; p++)							// CPUs keep any look nobody wears yet
	{
		int look = GetDriverLook(&players[p]);
		if (!players[p].isComputer)
			continue;
		else if (look >= 0 && !(taken & (1u << look)))
			taken |= 1u << look;
		else
			needNewLook |= 1u << p;
	}

	for (short p = 0; p < numPlayers; p++)							// the rest take a look nobody wears
	{
		if (!(needNewLook & (1u << p)))
			continue;

		int look = GetDriverLook(&players[p]);
		int otherSex = look < 0 ? -1 : (1 - players[p].sex) * NUM_CAVEMAN_SKINS + players[p].skin;

		if (otherSex >= 0 && !(taken & (1u << otherSex)))			// same skin, other sex
			look = otherSex;
		else
		{
			int unworn = 0;
			while (unworn < NUM_DRIVER_LOOKS && (taken & (1u << unworn)))
				unworn++;

			if (unworn < NUM_DRIVER_LOOKS)							// else the first unworn look
				look = unworn;
			else if (look < 0)										// every look is worn: any valid one
				look = p % NUM_DRIVER_LOOKS;
			else
				continue;
		}

		players[p].sex = look / NUM_CAVEMAN_SKINS;
		players[p].skin = look % NUM_CAVEMAN_SKINS;
		taken |= 1u << look;
	}
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
