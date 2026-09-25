#include "game.h"

#define VOLUME_DISTANCE_FACTOR 0.001f

// Each local camera contributes its own attenuation and stereo placement.
// Keep the loudest contribution to each channel so extra listeners don't sum
// identical effects and make split-screen play progressively louder.
void CalcSpatialAudioVolume(const OGLPoint3D* source, float refDistance, float volumeAdjust,
	const OGLPoint3D* ears, const OGLVector3D* eyes, int numListeners,
	uint32_t* leftOut, uint32_t* rightOut)
{
	*leftOut = *rightOut = 0;
	for (int i = 0; i < numListeners; i++)
	{
		float distance = CalcDistance3D(source->x, source->y, source->z,
			ears[i].x, ears[i].y, ears[i].z) - refDistance;
		float attenuation = distance <= 0 ? 1 : GAME_MIN(1.0f, 1.0f / (distance * VOLUME_DISTANCE_FACTOR));
		uint32_t volume = (float)FULL_CHANNEL_VOLUME * attenuation * volumeAdjust;
		if (volume < 6)
			continue;

		OGLVector2D earToSound, look;
		FastNormalizeVector2D(source->x - ears[i].x, source->z - ears[i].z, &earToSound, true);
		FastNormalizeVector2D(eyes[i].x, eyes[i].z, &look, true);
		float separation = 1 - fabsf(earToSound.x * look.x + earToSound.y * look.y);
		separation = GAME_CLAMP(separation, 0, 1);
		float cross = earToSound.x * look.y - earToSound.y * look.x;
		float loud = (float)volume + (float)volume * separation;
		float quiet = (float)volume - (float)volume * separation;
		uint32_t left = cross > 0 ? loud : quiet;
		uint32_t right = cross > 0 ? quiet : loud;
		*leftOut = GAME_MAX(*leftOut, left);
		*rightOut = GAME_MAX(*rightOut, right);
	}
}
