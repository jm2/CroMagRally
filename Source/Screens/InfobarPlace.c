/****************************/
/*    INFOBAR PLACE.C       */
/****************************/


#include "game.h"


// Places past the number sprites are drawn with font digits, and the
// announcer stays silent past its last line (see below).
_Static_assert(MAX_PLAYERS <= 999 && MAX_PLACE_DIGITS >= 3, "MAX_PLACE_DIGITS can't spell every place");


/********************* GET PLACE NUMBER ***********************/
//
// Returns how to draw a place's big number: its hand-drawn sprite for 1st-6th,
// or past those, the digits to draw with the wall font, whose tan carved
// stone matches the sprites.
//

PlaceNumber GetPlaceNumber(int place)
{
	PlaceNumber number = {0};

	place = GAME_MAX(place, 0);

	if (place < NUM_PLACE_SPRITES)
	{
		number.sprite = INFOBAR_SObjType_Place1 + place;
		return number;
	}

	int largest = 0;										// largest number that fits in MAX_PLACE_DIGITS
	for (int i = 0; i < MAX_PLACE_DIGITS; i++)
		largest = largest * 10 + 9;

	int value = GAME_MIN(place + 1, largest);

	for (int v = value; v > 0; v /= 10)
		number.numDigits++;

	for (int i = number.numDigits - 1; i >= 0; i--, value /= 10)
		number.digits[i] = (char) ('0' + value % 10);

	return number;
}


/********************* GET PLACE ORDINAL X ***********************/
//
// Where the ordinal sprite goes, in place sprite units right of where it
// sits after a number sprite. A sprite's number fills the left half of its
// cell and the ordinal the right half, so they meet at the cell's center.
// Font digits, scaled to the sprites' height, end where the ordinal begins
// and start where the sprites' numbers start: one digit fills the left half
// like a sprite, and each further digit pushes the ordinal right by one
// (the font's digits all have the same advance).
//

float GetPlaceOrdinalX(int numDigits, float digitAdvance)
{
	return (float) GAME_MAX(numDigits - 1, 0) * digitAdvance * PLACE_DIGIT_SCALE;
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
