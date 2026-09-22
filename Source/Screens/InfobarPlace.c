/****************************/
/*    INFOBAR PLACE.C       */
/****************************/


#include "game.h"


// Raising MAX_PLAYERS past a table must come with a deliberate choice for the
// extra places; until then the lookups below keep every place inside its table.
_Static_assert(NUM_PLACE_SPRITES >= MAX_PLAYERS, "no big place number for 7th+: add art/font support first");
_Static_assert(NUM_ANNOUNCER_PLACE_LINES >= MAX_PLAYERS, "no announcer line for 7th+: decide what to say (silence is safe)");


/********************* GET PLACE NUMBER SPRITE ***********************/
//
// Returns the big place number sprite (INFOBAR_SObjType_Place1..Place6).
// A place past the table shows the last number instead of whichever
// unrelated sprite follows Place6 in the atlas.
//

int GetPlaceNumberSprite(int place)
{
	return INFOBAR_SObjType_Place1 + GAME_CLAMP(place, 0, NUM_PLACE_SPRITES - 1);
}


/********************* GET PLACE ORDINAL SPRITE ***********************/
//
// Returns the ordinal suffix sprite drawn after a place number, e.g. the
// "th" of "12th". German, Spanish and Swedish use the English suffixes.
//

int GetPlaceOrdinalSprite(int place, int language, int sex)
{
	int number = GAME_MAX(place, 0) + 1;

	switch (language)
	{
		case LANGUAGE_ENGLISH:
		default:
			if (number % 100 >= 11 && number % 100 <= 13)		// 11th, 12th, 13th (not 11st)
				return INFOBAR_SObjType_PlaceTH;

			switch (number % 10)
			{
				case 1: return INFOBAR_SObjType_PlaceST;
				case 2: return INFOBAR_SObjType_PlaceND;
				case 3: return INFOBAR_SObjType_PlaceRD;
				default: return INFOBAR_SObjType_PlaceTH;
			}

		case LANGUAGE_FRENCH:									// 1er/1re, then 2e, 3e... 12e
			if (number == 1)
				return sex==1? INFOBAR_SObjType_PlaceRE: INFOBAR_SObjType_PlaceER;
			else
				return INFOBAR_SObjType_PlaceE;

		case LANGUAGE_ITALIAN:									// 1º/1ª... 12º/12ª
			return sex==1? INFOBAR_SObjType_PlaceA: INFOBAR_SObjType_PlaceO;
	}
}


/********************* GET PLACE ANNOUNCER EFFECT ***********************/
//
// Returns the announcer line for a final place (EFFECT_1st..EFFECT_6th),
// or -1 when there's no line for it, so the announcer stays silent rather
// than playing an unrelated effect.
//

int GetPlaceAnnouncerEffect(int place)
{
	if (place < 0 || place >= NUM_ANNOUNCER_PLACE_LINES)
		return -1;

	return EFFECT_1st + place;
}
