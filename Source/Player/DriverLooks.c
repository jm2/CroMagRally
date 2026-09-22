/****************************/
/*   	DRIVER LOOKS.C		*/
/****************************/

#include "game.h"
#include "driver_looks.h"


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

