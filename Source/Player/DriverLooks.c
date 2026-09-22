/****************************/
/*   	DRIVER LOOKS.C		*/
/****************************/

#include "game.h"
#include "driver_looks.h"

_Static_assert(NUM_CAVEMAN_SKINS <= 32, "skinsTaken holds one bit per outfit");
_Static_assert(MAX_PLAYERS <= 32, "lockedMask holds one bit per player");


static short WrapSkin(int skin)
{
	skin %= NUM_CAVEMAN_SKINS;
	return (short) (skin < 0 ? skin + NUM_CAVEMAN_SKINS : skin);
}

static Boolean IsValidLook(DriverLook look)
{
	return look.sex >= 0 && look.sex < NUM_DRIVER_SEXES
		&& look.skin >= 0 && look.skin < NUM_CAVEMAN_SKINS;
}

static Boolean SameLook(DriverLook a, DriverLook b)
{
	return a.sex == b.sex && a.skin == b.skin;
}

static Boolean IsLocked(uint32_t lockedMask, int playerNum)
{
	return playerNum < 32 && (lockedMask & (1u << playerNum));
}


/****************** GET DEFAULT DRIVER LOOK *********************/
//
// Keyed on the outfit rather than the slot so the waves stay distinct for any outfit count.
// With six outfits this is sex = (i&1) ^ ((i/6)&1): unchanged for the original six slots.
//

DriverLook GetDefaultDriverLook(int playerNum)
{
	if (playerNum < 0)
		playerNum = 0;

	const int skin = playerNum % NUM_CAVEMAN_SKINS;
	const int wave = playerNum / NUM_CAVEMAN_SKINS;

	return (DriverLook)
	{
		.sex	= (short) ((skin & 1) ^ (wave & 1)),			// alternate male/female, flipped every other wave
		.skin	= (short) skin,
	};
}


/****************** GET DEFAULT HUMAN DRIVER SEX *********************/

short GetDefaultHumanDriverSex(int playerNum)
{
	if (playerNum < 0)
		playerNum = 0;

	return (short) ((playerNum / NUM_CAVEMAN_SKINS) & 1);
}


/********************* CYCLE DRIVER SKIN ************************/

short CycleDriverSkin(short currentSkin, int delta, uint32_t skinsTaken)
{
	const int step = delta < 0 ? -1 : 1;
	short skin = WrapSkin(currentSkin);

	if (delta == 0 && !(skinsTaken & (1u << skin)))				// just making sure it's free
		return skin;

	for (int tries = 0; tries < NUM_CAVEMAN_SKINS; tries++)
	{
		skin = WrapSkin(skin + step);
		if (!(skinsTaken & (1u << skin)))
			return skin;
	}

	return WrapSkin(currentSkin + step);						// every outfit is taken: a repeat is unavoidable
}


/********************* SWAP DRIVER LOOK ************************/

int SwapDriverLook(DriverLook looks[], int numPlayers, int whichPlayer, DriverLook want, uint32_t lockedMask)
{
	if (whichPlayer < 0 || whichPlayer >= numPlayers)
		return -1;

	const DriverLook oldLook = looks[whichPlayer];
	int partner = -1;

	if (SameLook(oldLook, want))
		return -1;

			/* WHOEVER WEARS EXACTLY THIS LOOK TAKES MY OLD ONE */

	for (int i = 0; i < numPlayers && partner < 0; i++)
	{
		if (i != whichPlayer && !IsLocked(lockedMask, i) && SameLook(looks[i], want))
			partner = i;
	}

	if (partner >= 0)
	{
		looks[partner] = oldLook;
	}

			/* OTHERWISE SWAP OUTFITS WITH THE FIRST ONE WEARING IT */

	else if (want.skin != oldLook.skin)
	{
		for (int i = 0; i < numPlayers && partner < 0; i++)
		{
			if (i != whichPlayer && !IsLocked(lockedMask, i) && looks[i].skin == want.skin)
				partner = i;
		}

		if (partner >= 0)
			looks[partner].skin = oldLook.skin;
	}

	looks[whichPlayer] = want;
	return partner;
}


/********************* CYCLE DRIVER OUTFIT ************************/

short CycleDriverOutfit(DriverLook looks[], int numPlayers, int whichPlayer, int delta,
						uint32_t playersDone, bool dressOthers)
{
uint32_t	skinsTaken = 0;
uint32_t	looksTaken = 0;											// outfits taken with my body
const uint32_t allSkins = (1u << NUM_CAVEMAN_SKINS) - 1u;

	if (whichPlayer < 0 || whichPlayer >= numPlayers)
		return 0;

	for (int i = 0; i < numPlayers; i++)						// find out which skins are already taken
	{
		if (i != whichPlayer && IsLocked(playersDone, i) && IsValidLook(looks[i]))
		{
			skinsTaken |= 1u << looks[i].skin;
			if (looks[i].sex == looks[whichPlayer].sex)
				looksTaken |= 1u << looks[i].skin;
		}
	}

	if (skinsTaken == allSkins)									// every outfit chosen: keep whole looks unique instead
		skinsTaken = looksTaken;

	const short newSkin = CycleDriverSkin(looks[whichPlayer].skin, delta, skinsTaken);

	if (dressOthers)
		SwapDriverLook(looks, numPlayers, whichPlayer, (DriverLook) { .sex = looks[whichPlayer].sex, .skin = newSkin }, playersDone);
	else
		looks[whichPlayer].skin = newSkin;

	return newSkin;
}


/********************* CHANGE DRIVER BODY ************************/

void ChangeDriverBody(DriverLook looks[], int numPlayers, int whichPlayer, short sex,
						uint32_t playersDone, bool dressOthers)
{
	if (whichPlayer < 0 || whichPlayer >= numPlayers)
		return;

	if (dressOthers)
		SwapDriverLook(looks, numPlayers, whichPlayer, (DriverLook) { .sex = sex, .skin = looks[whichPlayer].skin }, playersDone);
	else
		looks[whichPlayer].sex = sex;
}


/******************** RESOLVE CPU DRIVER LOOKS ***********************/
//
// Each CPU in slot order keeps its look unless it repeats a look worn by a human or an earlier
// CPU, or repeats an earlier player's outfit while another outfit is unworn. It then takes the
// look worn least by everyone else, then the least worn outfit, then its own body, then the
// nearest following outfit. Looks worn by later CPUs count too, so one fix does not cascade.
//

int ResolveCPUDriverLooks(DriverLook looks[], int numPlayers, int numFixed)
{
int	numChanged = 0;

	if (numFixed < 0)
		numFixed = 0;

	for (int i = numFixed; i < numPlayers; i++)
	{
		int			lookUse[NUM_DRIVER_SEXES][NUM_CAVEMAN_SKINS] = {{0}};
		int			skinUse[NUM_CAVEMAN_SKINS] = {0};
		Boolean		repeatsLook = false;
		Boolean		repeatsSkin = false;
		Boolean		someSkinUnworn = false;
		const DriverLook own = looks[i];
		const Boolean ownValid = IsValidLook(own);

		for (int j = 0; j < numPlayers; j++)					// count what everyone else wears
		{
			if (j == i || !IsValidLook(looks[j]))
				continue;

			lookUse[looks[j].sex][looks[j].skin]++;
			skinUse[looks[j].skin]++;

			if (j < i && SameLook(looks[j], own))
				repeatsLook = true;
			if (j < i && looks[j].skin == own.skin)
				repeatsSkin = true;
		}

		for (int skin = 0; skin < NUM_CAVEMAN_SKINS; skin++)
		{
			if (skinUse[skin] == 0)
				someSkinUnworn = true;
		}

		if (ownValid && !repeatsLook && !(repeatsSkin && someSkinUnworn))
			continue;

				/* TAKE THE LEAST WORN LOOK */

		const DriverLook home = ownValid ? own : GetDefaultDriverLook(i);
		const int	useWeight = numPlayers + 1;					// a use count never reaches this
		DriverLook	best = home;
		int			bestScore = -1;

		for (int n = 0; n < NUM_CAVEMAN_SKINS; n++)				// own look wins ties, then the nearest outfit
		{
			const short skin = WrapSkin(home.skin + n);

			for (int otherBody = 0; otherBody < NUM_DRIVER_SEXES; otherBody++)
			{
				const short sex = (short) ((home.sex + otherBody) % NUM_DRIVER_SEXES);
				const int score = (lookUse[sex][skin] * useWeight + skinUse[skin]) * NUM_DRIVER_SEXES + otherBody;

				if (bestScore < 0 || score < bestScore)
				{
					bestScore = score;
					best = (DriverLook) { .sex = sex, .skin = skin };
				}
			}
		}

		if (!SameLook(best, own))
		{
			looks[i] = best;
			numChanged++;
		}
	}

	return numChanged;
}

