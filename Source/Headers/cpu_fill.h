//
// cpu_fill.h
//
// CPU slot fill: CPU cars race in the grid slots that no human takes in a
// multiplayer race. These rules read no game state, so they can be unit tested
// and give every network peer the same answer.
//

#pragma once

enum
{
	kRaceResult_None = 0,		// no win/lose message for this player
	kRaceResult_Won,
	kRaceResult_Lost,
};

// Whether CPU cars can fill a game mode's empty slots. Only multiplayer races:
// battle arenas have no AI paths, and the CPU driver knows no battle rules.
Boolean CPUFillAppliesToMode(int gameMode);

// Whether CPU cars fill this game's empty slots. A local game takes this machine's pref.
// A network game takes the host's choice from its game config (the host sends its own
// pref) on every peer, so a client's own pref never changes which cars race.
Boolean DecideCPUFillThisRace(int gameMode, Boolean netGame, Boolean hostConfigCPUFill, Boolean prefCPUFill);

// How many cars race in a game. Humans take slots 0..numRealPlayers-1 and CPU cars
// the rest. Single-player races use every slot; multiplayer races only with CPU fill
// (Pangea's had none); battle modes never seat CPU cars.
short CountPlayersInGame(int gameMode, short numRealPlayers, Boolean cpuFill);

// Keeps CPU drivers from looking like the humans or each other, while looks (sex and
// skin) are left. Humans keep what they picked, and so does every CPU whose look is
// still unworn. Each other CPU (in slot order) takes the other sex in its own skin if
// that is unworn, else the first unworn look. With more players than looks, the
// leftover CPUs keep theirs. numPlayers must not exceed 32.
void MakeCPULooksDistinct(PlayerInfoType players[], short numPlayers);

// Network CPU fill: dresses the CPUs in slots numHumans..numPlayers-1 alike on every peer.
// Each starts from the look InitPlayerInfo_Game dealt its slot, whatever this machine's
// character screen swapped into it. Then MakeCPULooksDistinct's rule applies, with every
// human slot keeping its look (a player who has since left included, bot or not).
// numPlayers must not exceed 32.
void DressNetworkFillCPUs(PlayerInfoType players[], short numHumans, short numPlayers);

// finisher just completed a multiplayer race. Returns true if that ends the race,
// and then sets results[p] for each of the numPlayers players.
// Only humans contest a multiplayer race: CPU cars (fill) and network bots (players who
// left) race for places but never end it. The first human home wins, the other humans
// lose, and computer-driven cars get no result. Without fill or departures this is the
// original rule: the first car home wins and everyone else loses.
// raceDecided: an earlier finish already ended the race.
Boolean DecideMultiplayerRaceFinish(const PlayerInfoType players[], short numPlayers, short finisher,
		Boolean raceDecided, Byte results[]);
