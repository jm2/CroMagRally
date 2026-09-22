#pragma once

#include <stdbool.h>
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

// Outfit after currentSkin in direction delta (+1 or -1), skipping the outfits set in skinsTaken
// (bit per CAVEMAN_SKIN_*). When every outfit is taken it steps to the adjacent one anyway.
// Delta 0 keeps currentSkin if it is free, and otherwise steps forward.
short CycleDriverSkin(short currentSkin, int delta, uint32_t skinsTaken);

// Dresses whichPlayer in `want` without changing the set of looks in use: the player who wore
// exactly `want` takes whichPlayer's old look. If nobody did, the first player in the same outfit
// swaps outfits instead (the original rule, where each outfit is worn once). Players in lockedMask
// are never changed. Returns the partner, or -1 if there was none.
int SwapDriverLook(DriverLook looks[], int numPlayers, int whichPlayer, DriverLook want, uint32_t lockedMask);

// The character select screen's outfit change. Players in playersDone chose already: their
// outfits are skipped while another is free (after that, only their looks with whichPlayer's
// body are), and their looks never change. Delta 0 moves whichPlayer off an outfit they took,
// and otherwise changes nothing. With dressOthers, whoever wore the new look takes
// whichPlayer's old one (SwapDriverLook); without it (network games, where everyone chooses on
// their own machine) only whichPlayer changes. Returns the new outfit.
short CycleDriverOutfit(DriverLook looks[], int numPlayers, int whichPlayer, int delta,
						uint32_t playersDone, bool dressOthers);

// The character select screen's body choice, with the same rules as CycleDriverOutfit.
void ChangeDriverBody(DriverLook looks[], int numPlayers, int whichPlayer, short sex,
						uint32_t playersDone, bool dressOthers);

// Re-dresses CPU slots numFixed...numPlayers-1 whose look repeats a human's or an earlier CPU's,
// or whose outfit repeats an earlier player's while some outfit is unworn. Humans keep what they
// chose. A pure function of its input, so every network peer gets the same result. Past
// NUM_DRIVER_SEXES * NUM_CAVEMAN_SKINS players the repeats are spread evenly. Returns how many
// CPUs changed.
int ResolveCPUDriverLooks(DriverLook looks[], int numPlayers, int numFixed);

// How many of players 0...playerNum-1 wear playerNum's outfit. Minimap blips are coloured by
// outfit (kCavemanSkinColors), so from rank 1 on a blip needs a marker to stand apart.
int GetDriverOutfitRank(const DriverLook looks[], int playerNum);

// Grey level (0 = black, 1 = white) for a marker drawn over a blip of this fill colour:
// whichever contrasts more with it.
float GetBlipMarkerShade(float r, float g, float b);
