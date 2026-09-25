#include "game.h"
#include <limits.h>

// Supertile player masks hold one bit per player; SeeIfCoordsOutOfRange builds 16-bit masks.
_Static_assert(MAX_PLAYERS <= 16, "terrain player masks are 16 bits wide");
_Static_assert(sizeof(((SuperTileStatus *)0)->playerHereFlags) * CHAR_BIT >= MAX_PLAYERS,
               "SuperTileStatus.playerHereFlags needs one bit per player");

/***************** KEEP TERRAIN ALIVE FOR RENDER ******************/
//
// DrawTerrain clears SUPERTILE_IS_USED_THIS_FRAME after each rendered frame.
// If no simulation step ran since then, the previously active terrain set is
// still current, but DoPlayerTerrainUpdate hasn't re-marked it. Preserve only
// the already-loaded geometry without advancing terrain-item streaming state.
//

void KeepTerrainAliveForRender(void) {
  for (int row = 0; row < gNumSuperTilesDeep; row++) {
    for (int col = 0; col < gNumSuperTilesWide; col++) {
      if (gSuperTileStatusGrid[row][col].statusFlags & SUPERTILE_IS_DEFINED)
        gSuperTileStatusGrid[row][col].statusFlags |=
            SUPERTILE_IS_USED_THIS_FRAME;
    }
  }
}

/***************** SUPERTILE PLAYER FLAGS ******************/
//
// Terrain items stay alive while any player's item ring covers their supertile,
// so every player needs its own bit. With the original 8-bit field, players 8+
// would silently lose the items around them.
//

void MarkSuperTilePlayerHere(SuperTileStatus *status, short playerNum) {
  status->playerHereFlags |= (uint16_t)(1u << playerNum);
}

Boolean IsSuperTileUsedByPlayers(const SuperTileStatus *status,
                                 short playerToSkip) {
  uint16_t mask = 0xffff;

  if (playerToSkip != -1) // see if dont check for a player
    mask &= (uint16_t)~(1u << playerToSkip);

  return (status->playerHereFlags & mask) != 0;
}
