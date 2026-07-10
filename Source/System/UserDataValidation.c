#include "game.h"
#include <string.h>

static Boolean IsValidVolume(Byte volume)
{
	return volume <= 100 && volume % 20 == 0;
}

static Boolean IsValidPlayerName(const char playerName[32])
{
	const char* terminator = memchr(playerName, '\0', 32);
	if (!terminator)
		return false;

	for (const unsigned char* c = (const unsigned char*) playerName; *c; c++)
	{
		if (*c < 0x20 || *c == 0x7f)
			return false;
	}

	return true;
}

static Boolean IsValidPadBinding(const PadBinding* binding)
{
	switch (binding->type)
	{
		case kInputTypeUnbound:
			return binding->id == 0;

		case kInputTypeButton:
			return binding->id >= 0 && binding->id < SDL_GAMEPAD_BUTTON_COUNT;

		case kInputTypeAxisPlus:
		case kInputTypeAxisMinus:
			return binding->id >= 0 && binding->id < SDL_GAMEPAD_AXIS_COUNT;

		default:
			return false;
	}
}

Boolean SanitizePrefs(PrefsType* prefs, const PrefsType* defaults)
{
	if (!prefs || !defaults)
		return false;

	Boolean changed = false;

#define REPAIR_FIELD_IF_INVALID(field, condition) \
	do \
	{ \
		if (!(condition)) \
		{ \
			prefs->field = defaults->field; \
			changed = true; \
		} \
	} while (0)

	REPAIR_FIELD_IF_INVALID(difficulty, prefs->difficulty < NUM_DIFFICULTIES);
	REPAIR_FIELD_IF_INVALID(splitScreenMode2P,
		prefs->splitScreenMode2P == SPLITSCREEN_MODE_2P_TALL
		|| prefs->splitScreenMode2P == SPLITSCREEN_MODE_2P_WIDE);
	REPAIR_FIELD_IF_INVALID(splitScreenMode3P,
		prefs->splitScreenMode3P == SPLITSCREEN_MODE_3P_TALL
		|| prefs->splitScreenMode3P == SPLITSCREEN_MODE_3P_WIDE);
	REPAIR_FIELD_IF_INVALID(language, prefs->language < NUM_LANGUAGES);
	REPAIR_FIELD_IF_INVALID(tagDuration, prefs->tagDuration >= 2 && prefs->tagDuration <= 4);
	REPAIR_FIELD_IF_INVALID(antialiasingLevel, prefs->antialiasingLevel <= 3);
	REPAIR_FIELD_IF_INVALID(fullscreen, prefs->fullscreen <= 1);
	REPAIR_FIELD_IF_INVALID(musicVolumePercent, IsValidVolume(prefs->musicVolumePercent));
	REPAIR_FIELD_IF_INVALID(sfxVolumePercent, IsValidVolume(prefs->sfxVolumePercent));
	REPAIR_FIELD_IF_INVALID(raceTimer, prefs->raceTimer <= 2);
	REPAIR_FIELD_IF_INVALID(gamepadRumble, prefs->gamepadRumble <= 1);
	REPAIR_FIELD_IF_INVALID(tournamentProgression.numTracksCompleted,
		prefs->tournamentProgression.numTracksCompleted <= NUM_RACE_TRACKS);

#undef REPAIR_FIELD_IF_INVALID

	for (int track = 0; track < NUM_RACE_TRACKS; track++)
	{
		for (int lap = 0; lap < LAPS_PER_RACE; lap++)
		{
			float* lapTime = &prefs->tournamentProgression.tournamentLapTimes[track][lap];
			if (!isfinite(*lapTime) || *lapTime < 0 || *lapTime > MAX_SAVED_LAP_TIME_SECONDS)
			{
				*lapTime = defaults->tournamentProgression.tournamentLapTimes[track][lap];
				changed = true;
			}
		}
	}

	for (int need = 0; need < NUM_CONTROL_NEEDS; need++)
	{
		// UI bindings are intentionally fixed. Restoring only out-of-range values would
		// let a range-valid corrupt value disable Confirm/Back and strand the user.
		if (need >= NUM_REMAPPABLE_NEEDS)
		{
			if (SDL_memcmp(&prefs->bindings[need], &defaults->bindings[need], sizeof(InputBinding)) != 0)
			{
				prefs->bindings[need] = defaults->bindings[need];
				changed = true;
			}
			continue;
		}

		for (int binding = 0; binding < MAX_BINDINGS_PER_NEED; binding++)
		{
			// The last slot is an internal hard binding, not exposed in the controls UI.
			if (binding >= MAX_USER_BINDINGS_PER_NEED)
			{
				if (prefs->bindings[need].key[binding] != defaults->bindings[need].key[binding]
					|| SDL_memcmp(&prefs->bindings[need].pad[binding],
						&defaults->bindings[need].pad[binding], sizeof(PadBinding)) != 0)
				{
					prefs->bindings[need].key[binding] = defaults->bindings[need].key[binding];
					prefs->bindings[need].pad[binding] = defaults->bindings[need].pad[binding];
					changed = true;
				}
				continue;
			}

			int16_t* scancode = &prefs->bindings[need].key[binding];
			if (*scancode < 0 || *scancode >= SDL_SCANCODE_COUNT)
			{
				*scancode = defaults->bindings[need].key[binding];
				changed = true;
			}

			PadBinding* pad = &prefs->bindings[need].pad[binding];
			if (!IsValidPadBinding(pad))
			{
				*pad = defaults->bindings[need].pad[binding];
				changed = true;
			}
		}

		if (prefs->bindings[need].mouseButton < 0
			|| prefs->bindings[need].mouseButton >= NUM_SUPPORTED_MOUSE_BUTTONS)
		{
			prefs->bindings[need].mouseButton = defaults->bindings[need].mouseButton;
			changed = true;
		}
	}

	if (!IsValidPlayerName(prefs->playerName))
	{
		SDL_memcpy(prefs->playerName, defaults->playerName, sizeof(prefs->playerName));
		prefs->playerName[sizeof(prefs->playerName) - 1] = '\0';
		changed = true;
	}

	return changed;
}

static Boolean IsEmptyScoreboardRecord(const ScoreboardRecord* record)
{
	static const ScoreboardRecord emptyRecord = {0};
	return SDL_memcmp(record, &emptyRecord, sizeof(*record)) == 0;
}

static Boolean IsValidScoreboardRecord(const ScoreboardRecord* record, int track)
{
	for (int lap = 0; lap < LAPS_PER_RACE; lap++)
	{
		if (!isfinite(record->lapTimes[lap])
			|| record->lapTimes[lap] < 0.1f
			|| record->lapTimes[lap] > MAX_SAVED_LAP_TIME_SECONDS)
		{
			return false;
		}
	}

	return record->timestamp > 0
		&& record->timestamp <= INT64_MAX / SDL_NS_PER_SECOND
		&& record->trackNum == track
		&& record->difficulty < NUM_DIFFICULTIES
		&& (record->gameMode == GAME_MODE_PRACTICE
			|| record->gameMode == GAME_MODE_TOURNAMENT
			|| record->gameMode == GAME_MODE_MULTIPLAYERRACE)
		&& record->vehicleType < NUM_CAR_TYPES_TOTAL
		&& record->place < MAX_PLAYERS
		&& record->sex <= 1
		&& record->skin < NUM_CAVEMAN_SKINS;
}

Boolean SanitizeScoreboard(Scoreboard* scoreboard)
{
	if (!scoreboard)
		return false;

	Boolean changed = false;

	for (int track = 0; track < NUM_RACE_TRACKS; track++)
	{
		int writeIndex = 0;

		for (int readIndex = 0; readIndex < MAX_RECORDS_PER_TRACK; readIndex++)
		{
			ScoreboardRecord* record = &scoreboard->records[track][readIndex];
			if (IsEmptyScoreboardRecord(record))
				continue;

			if (!IsValidScoreboardRecord(record, track))
			{
				changed = true;
				continue;
			}

			if (writeIndex != readIndex)
			{
				scoreboard->records[track][writeIndex] = *record;
				changed = true;
			}
			writeIndex++;
		}

		for (; writeIndex < MAX_RECORDS_PER_TRACK; writeIndex++)
		{
			ScoreboardRecord* record = &scoreboard->records[track][writeIndex];
			if (!IsEmptyScoreboardRecord(record))
			{
				SDL_memset(record, 0, sizeof(*record));
				changed = true;
			}
		}
	}

	return changed;
}
