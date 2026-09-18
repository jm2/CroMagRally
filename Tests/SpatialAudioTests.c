#include "game.h"
#include <stdio.h>
#include <stdlib.h>

#define CHECK(condition) do { if (!(condition)) { \
	fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); \
	exit(EXIT_FAILURE); } } while (0)

int main(void)
{
	const OGLPoint3D sound = {100, 0, 0};
	OGLPoint3D ears[MAX_LOCAL_PLAYERS] = {0};
	OGLVector3D eyes[MAX_LOCAL_PLAYERS] = {{0, 0, 1}, {0, 0, -1}, {0, 0, 1}, {0, 0, -1}};
	uint32_t left, right;
	CalcSpatialAudioVolume(&sound, 100, 1, ears, eyes, 0, &left, &right);
	CHECK(left == 0 && right == 0);
	CalcSpatialAudioVolume(&sound, 100, 1, ears, eyes, 1, &left, &right);
	CHECK(left == 2 * FULL_CHANNEL_VOLUME && right == 0);
	CalcSpatialAudioVolume(&sound, 100, 1, ears, eyes, 2, &left, &right);
	CHECK(left == 2 * FULL_CHANNEL_VOLUME && right == 2 * FULL_CHANNEL_VOLUME);
	// A distant camera must not inherit a nearby camera's loudness.
	ears[1].x = -1000000;
	CalcSpatialAudioVolume(&sound, 100, 1, ears, eyes, 2, &left, &right);
	CHECK(left == 2 * FULL_CHANNEL_VOLUME && right == 0);
	for (int count = 1; count <= MAX_LOCAL_PLAYERS; count++)
	{
		for (int near = 0; near < count; near++)
		{
			for (int i = 0; i < MAX_LOCAL_PLAYERS; i++)
				ears[i] = (OGLPoint3D){-1000000, 0, 0};
			ears[near] = (OGLPoint3D){0, 0, 0};
			CalcSpatialAudioVolume(&sound, 100, 1, ears, eyes, count, &left, &right);
			CHECK((near % 2 == 0 && left == 2 * FULL_CHANNEL_VOLUME && right == 0)
				|| (near % 2 == 1 && right == 2 * FULL_CHANNEL_VOLUME && left == 0));
		}
	}
	// Distance attenuation, volume adjustment, and front/back centering.
	ears[0] = (OGLPoint3D){-2000, 0, 0};
	eyes[0] = (OGLVector3D){1, 0, 0};
	CalcSpatialAudioVolume(&sound, 100, 0.5f, ears, eyes, 1, &left, &right);
	CHECK(left == FULL_CHANNEL_VOLUME / 4 && right == left);
	eyes[0].x = -1;
	CalcSpatialAudioVolume(&sound, 100, 0.5f, ears, eyes, 1, &left, &right);
	CHECK(left == FULL_CHANNEL_VOLUME / 4 && right == left);
	puts("Spatial audio tests passed");
	return 0;
}
