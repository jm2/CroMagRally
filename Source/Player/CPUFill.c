/****************************/
/*   	CPUFILL.C			*/
/****************************/

#include "game.h"
#include "cpu_fill.h"
#include "driver_looks.h"

#define	NUM_DRIVER_LOOKS	(2 * NUM_CAVEMAN_SKINS)				// both sexes in every skin

_Static_assert(NUM_DRIVER_LOOKS <= 32, "a uint32_t holds one bit per driver look");


/******************** CPU FILL APPLIES TO MODE *********************/

Boolean CPUFillAppliesToMode(int gameMode)
{
	return gameMode == GAME_MODE_MULTIPLAYERRACE;
}


/******************** DECIDE CPU FILL THIS RACE *********************/

Boolean DecideCPUFillThisRace(int gameMode, Boolean netGame, Boolean hostConfigCPUFill, Boolean prefCPUFill)
{
	return CPUFillAppliesToMode(gameMode) && (netGame ? hostConfigCPUFill : prefCPUFill);
}


/******************** DECIDE PLAYER LIMIT THIS GAME *********************/

Byte DecidePlayerLimitThisGame(Boolean netGame, int hostConfigLimit, int prefLimit)
{
	int limit = netGame ? hostConfigLimit : prefLimit;

	return (Byte) (IS_SUPPORTED_PLAYER_LIMIT(limit) ? limit : PLAYER_LIMIT_ORIGINAL);
}


/******************** SMALLEST PLAYER LIMIT FOR *********************/

Byte SmallestPlayerLimitFor(int numPlayers)
{
	return numPlayers <= PLAYER_LIMIT_ORIGINAL ? PLAYER_LIMIT_ORIGINAL : MAX_PLAYERS;
}


/******************** COUNT PLAYERS IN GAME *********************/

short CountPlayersInGame(int gameMode, short numRealPlayers, Boolean cpuFill, short playerLimit)
{
	short	fullGrid = playerLimit > numRealPlayers ? playerLimit : numRealPlayers;	// every human races

	switch (gameMode)
	{
		case	GAME_MODE_PRACTICE:
		case	GAME_MODE_TOURNAMENT:
				return fullGrid;

		default:
				if (cpuFill && CPUFillAppliesToMode(gameMode))
					return fullGrid;
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

// keep: bit p set for each player whose look is fixed (a human's).
static void MakeLooksDistinct(PlayerInfoType players[], short numPlayers, uint32_t keep)
{
uint32_t	taken = 0;
uint32_t	needNewLook = 0;

	for (short p = 0; p < numPlayers; p++)							// humans wear what they picked
	{
		int look = GetDriverLook(&players[p]);
		if ((keep & (1u << p)) && look >= 0)
			taken |= 1u << look;
	}

	for (short p = 0; p < numPlayers; p++)							// CPUs keep any look nobody wears yet
	{
		int look = GetDriverLook(&players[p]);
		if (keep & (1u << p))
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

void MakeCPULooksDistinct(PlayerInfoType players[], short numPlayers)
{
uint32_t	humans = 0;

	for (short p = 0; p < numPlayers; p++)
	{
		if (!players[p].isComputer)
			humans |= 1u << p;
	}

	MakeLooksDistinct(players, numPlayers, humans);
}


/******************** DRESS NETWORK FILL CPUS *********************/
//
// A character screen could swap outfits between the local player and whoever wears the
// one it wants, CPU slots included, and only on that machine. Starting every fill CPU from
// its dealt look again undoes that; ResolveCPUDriverLooks then keeps them apart from the
// humans and each other, reading only state every peer shares.
//

void DressNetworkFillCPUs(PlayerInfoType players[], short numHumans, short numPlayers)
{
	for (short p = numHumans; p < numPlayers; p++)					// the look InitPlayerInfo_Game dealt the slot
	{
		const DriverLook look = GetDefaultDriverLook(p);
		players[p].sex = look.sex;
		players[p].skin = look.skin;
	}
}


/******************** DECIDE MULTIPLAYER RACE FINISH *********************/
//
// Each human keeps seeing their place among all the cars: a CPU that finishes
// stays ahead of every car still racing (CalcPlayerPlaces).
//

Boolean DecideMultiplayerRaceFinish(const PlayerInfoType players[], short numPlayers, short finisher,
		Boolean raceDecided, Byte results[])
{
	if (raceDecided || finisher < 0 || finisher >= numPlayers)
		return false;

	if (players[finisher].isComputer)								// CPUs and network bots never end the race
		return false;

	for (short p = 0; p < numPlayers; p++)
	{
		if (p == finisher)
			results[p] = kRaceResult_Won;
		else if (players[p].isComputer)								// only humans contest the race
			results[p] = kRaceResult_None;
		else
			results[p] = kRaceResult_Lost;
	}

	return true;
}
