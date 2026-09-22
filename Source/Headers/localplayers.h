#pragma once

#include <stdbool.h>

// A "local slot" is what one human on *this* machine owns: a split-screen pane (with its camera,
// sound listener and HUD icons) and a gamepad slot. It is not a player number. In split-screen,
// player i uses local slot i. A network machine has exactly one human, who always uses slot 0
// whatever player number the host assigned it (up to MAX_PLAYERS-1). Arrays sized
// MAX_LOCAL_PLAYERS, MAX_SPLITSCREENS or MAX_VIEWPORTS must be indexed by local slot.
//
// Returns playerNum's local slot (0..MAX_LOCAL_PLAYERS-1), or -1 if playerNum is not a human
// on this machine. GetPlayerNum(slot) (globals.h) is the inverse for the current session.
int LocalSlotForPlayer(int playerNum, bool netGame, int myNetworkPlayerNum, int numLocalPlayers);

#define GetLocalSlotForPlayer(playerNum) \
	LocalSlotForPlayer((playerNum), gNetGameInProgress, gMyNetworkPlayerNum, gNumLocalPlayers)
