#include "game.h"

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
