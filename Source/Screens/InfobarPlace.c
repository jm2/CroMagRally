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
