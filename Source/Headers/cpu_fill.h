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

// How many cars race in a game. Humans take slots 0..numRealPlayers-1 and CPU cars
// the rest. Single-player races use every slot; multiplayer races only with CPU fill
// (Pangea's had none); battle modes never seat CPU cars.
short CountPlayersInGame(int gameMode, short numRealPlayers, Boolean cpuFill);

// finisher just completed a multiplayer race. Returns true if that ends the race,
// and then sets results[p] for each of the numPlayers players.
// Without CPU fill the first car home wins and everyone else loses, as always (that
// includes a network bot). With CPU fill, CPU cars race for places but never end the
// race: the first human home wins, the other humans lose, and CPUs get no result.
// raceDecided: an earlier finish already ended the race.
Boolean DecideMultiplayerRaceFinish(const PlayerInfoType players[], short numPlayers, short finisher,
		Boolean raceDecided, Boolean cpuFill, Byte results[]);
