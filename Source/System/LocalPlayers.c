#include "main.h"
#include "localplayers.h"

int LocalSlotForPlayer(int playerNum, bool netGame, int myNetworkPlayerNum, int numLocalPlayers)
{
	if (playerNum < 0 || playerNum >= MAX_PLAYERS)
		return -1;

	if (netGame)												// one human per network machine, always in slot 0
		return playerNum == myNetworkPlayerNum ? 0 : -1;

	if (playerNum < numLocalPlayers && playerNum < MAX_LOCAL_PLAYERS)	// split-screen: player i owns slot i
		return playerNum;

	return -1;
}
