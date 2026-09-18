#include "game.h"
#include <float.h>
#include <stdio.h>
#include <stdlib.h>

#define CHECK(condition) do { if (!(condition)) { \
	fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); \
	exit(EXIT_FAILURE); } } while (0)

long gNumSuperTilesDeep = 1, gNumSuperTilesWide = 1;
extern SuperTilePathGridType** gSuperTilePathGrid;
void* AllocPtr(long size) { void* p = calloc(1, size); CHECK(p); return p; }
void SafeDisposePtr(void* p) { free(p); }
void DoFatalAlert(const char* format, ...) { (void)format; abort(); }
float GetTerrainY(float x, float z) { (void)x; (void)z; return 100; }

static void LoadPath(const PathPointType* points, int count)
{
	gNumPaths = 1;
	gPathList = (PathDefType**)NewHandle(sizeof(PathDefType));
	CHECK(gPathList);
	PathDefType* path = *gPathList;
	memset(path, 0, sizeof(*path));
	path->numPoints = count;
	path->pointList = (PathPointType**)NewHandle(count * sizeof(*points));
	CHECK(path->pointList);
	memcpy(*path->pointList, points, count * sizeof(*points));
	AssignPathVectorsToSuperTileGrid();
}

static void CheckUnit(OGLVector2D v)
{
	CHECK(isfinite(v.x) && isfinite(v.y));
	CHECK(fabsf(v.x * v.x + v.y * v.y - 1) < 0.001f);
}

int main(void)
{
	const PathPointType points[] = {{10, 10}, {20, 10}, {20, 20}, {20, 20}};
	LoadPath(points, 4);
	SuperTilePathGridType* cell = &gSuperTilePathGrid[0][0];
	CHECK(cell->numVectors == 4);
	for (int i = 0; i < 4; i++)
	{
		PathVectorType p = cell->vectors[i];
		CheckUnit((OGLVector2D){p.vx, p.vz});
		OGLVector2D v;
		CHECK(CalcPathVectorFromCoord(p.ox, 100, p.oz, &v));
		CheckUnit(v);
		CHECK(CalcPathVectorFromCoord(p.ox + 0.00001f, 100, p.oz, &v));
		CheckUnit(v);
	}
	CHECK(cell->vectors[3].vx == cell->vectors[1].vx);
	CHECK(cell->vectors[3].vz == cell->vectors[1].vz);
	CHECK(cell->vectors[3].vz > 0.99f);

	// Ordinary inverse-distance blending, including duplicate origins.
	cell->numVectors = 2;
	cell->vectors[0] = (PathVectorType){100, 100, 1, 0};
	cell->vectors[1] = (PathVectorType){100, 100, 0, 1};
	OGLVector2D v;
	CHECK(CalcPathVectorFromCoord(100, 100, 100, &v));
	CHECK(fabsf(v.x - 0.7071068f) < 0.001f && fabsf(v.y - v.x) < 0.001f);
	CHECK(CalcPathVectorFromCoord(200, 100, 200, &v));
	CheckUnit(v);
	cell->vectors[1] = (PathVectorType){100, 100, -1, 0};
	CHECK(!CalcPathVectorFromCoord(100, 100, 100, &v));
	CHECK(v.x == 0 && v.y == 0);
	cell->numVectors = 1;
	cell->flags[0] = PATH_FLAGS_YCLOSE;
	CHECK(!CalcPathVectorFromCoord(100, 200, 100, &v));
	CHECK(!CalcPathVectorFromCoord(NAN, 100, 100, &v));
	CHECK(!CalcPathVectorFromCoord(FLT_MAX, 100, FLT_MAX, &v));
	DisposePathGrid();
	CHECK(!CalcPathVectorFromCoord(100, 100, 100, &v));

	const PathPointType coincident[] = {{10, 10}, {10, 10}};
	LoadPath(coincident, 2);
	CHECK(!CalcPathVectorFromCoord(10 * MAP2UNIT_VALUE, 100, 10 * MAP2UNIT_VALUE, &v));
	CHECK(v.x == 0 && v.y == 0);
	DisposePathGrid();
	puts("Path direction tests passed");
	return 0;
}
