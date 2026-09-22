#pragma once

#include <stdint.h>

// Driver looks, kept free of game state so the rules can be tested exhaustively.
// A look is a body (sex) and an outfit (skin): 2 bodies x NUM_CAVEMAN_SKINS outfits.

#define	NUM_DRIVER_SEXES	2				// 0 = Brog (male), 1 = Grag (female)

typedef struct
{
	short	sex;
	short	skin;							// CAVEMAN_SKIN_*
} DriverLook;

// Look for slot playerNum before anyone chooses. The first NUM_CAVEMAN_SKINS slots alternate
// Brog/Grag and wear every outfit once, like the original six-car grid. Each later wave repeats
// the outfits with the bodies flipped, so the first NUM_DRIVER_SEXES * NUM_CAVEMAN_SKINS slots
// all look different.
DriverLook GetDefaultDriverLook(int playerNum);

// Body the character select screen offers a human first: Brog, as in the original game, except
// in the odd waves (slots 6-11), where Grag keeps humans who accept the defaults distinct.
short GetDefaultHumanDriverSex(int playerNum);

// Re-dresses CPU slots numFixed...numPlayers-1 whose look repeats a human's or an earlier CPU's,
// or whose outfit repeats an earlier player's while some outfit is unworn. Humans keep what they
// chose. A pure function of its input, so every network peer gets the same result. Past
// NUM_DRIVER_SEXES * NUM_CAVEMAN_SKINS players the repeats are spread evenly. Returns how many
// CPUs changed.
int ResolveCPUDriverLooks(DriverLook looks[], int numPlayers, int numFixed);
