//
// infobar.h
//

#define	MAX_TOKENS		8

// n minutes per player; in elimination tag, shorter above six players (car_count_tuning.h)
#define	TAG_TIME_LIMIT	((float)(gTagDuration) * 60.0 \
						* (gGameMode == GAME_MODE_TAG1 ? GetEliminationTagTimeScale(gNumTotalPlayers) : 1.0f))

#define	MAX_PLACE_DIGITS	3				// places past the number sprites are drawn with font digits
#define	PLACE_DIGIT_SCALE	1.4f			// the font's digits are ~56 units tall, the place sprites ~79

typedef struct
{
	int		sprite;							// big number sprite for 1st-6th, else INFOBAR_SObjType_NULL
	int		numDigits;						// past those, how many font digits to draw instead
	char	digits[MAX_PLACE_DIGITS + 1];	// those digits as a string, e.g. "12"
} PlaceNumber;


void InitInfobar(void);
void DisposeInfobar(void);
void ShowLapNum(short playerNum);
void ShowFinalPlace(short playerNum, int rankInScoreboard);
PlaceNumber GetPlaceNumber(int place);
float GetPlaceOrdinalX(int numDigits, float digitAdvance);
int GetPlaceOrdinalSprite(int place, int language, int sex);
int GetPlaceAnnouncerEffect(int place);
void DecCurrentPOWQuantity(short playerNum);
void ShowWinLose(short playerNum, Byte mode, short winner);
void MakeIntroTrackName(void);
