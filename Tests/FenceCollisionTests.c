#include "game.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define CHECK(condition) do { if (!(condition)) { \
	fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); \
	exit(EXIT_FAILURE); } } while (0)

// Link stubs. DoFenceCollision and ReflectVector2D use only gCoord/gDelta and the fence
// list; the rest is referenced by fence drawing and other 3DMath.c functions.
OGLPoint3D				gCoord;
OGLVector3D				gDelta;
float					gFramesPerSecondFrac = 1.0f / 60.0f;
OGLSetupOutputType		*gGameView = NULL;
OGLMatrix4x4			gLocalToFrustumMatrix;
uint32_t				gGlobalMaterialFlags = 0;
Byte					gDebugMode = 0;
int						gCurrentSplitScreenPane = 0;
float					gAutoFadeStartDist = 0, gAutoFadeEndDist = 0, gAutoFadeRange_Frac = 0;
void* AllocPtr(long size) { void* p = calloc(1, size); CHECK(p); return p; }
void SafeDisposePtr(void* p) { free(p); }
void DoFatalAlert(const char* format, ...) { (void)format; abort(); }
float GetTerrainY(float x, float z) { (void)x; (void)z; return 0; }
ObjNode* MakeNewObject(NewObjectDefinitionType* def) { (void)def; abort(); }
void MO_DisposeObjectReference(MetaObjectPtr obj) { (void)obj; }
void MO_DrawGeometry_VertexArray(const MOVertexArrayData* data) { (void)data; abort(); }
MOMaterialObject* MO_GetTextureFromFile(const char* path, int destPixelFormat) { (void)path; (void)destPixelFormat; abort(); }
Boolean IsPointInTriangle(float pt_x, float pt_y, float x0, float y0, float x1, float y1, float x2, float y2)
{
	(void)pt_x; (void)pt_y; (void)x0; (void)y0; (void)x1; (void)y1; (void)x2; (void)y2;
	abort();
}

#define RADIUS 150.0f

static OGLPoint3D		gTestNubs[2];
static OGLVector2D		gTestSectionVector, gTestSectionNormal;
static FenceDefType		gTestFence;

// One straight fence on z = 0, set up the way PrimeFences does it.
static void MakeFence(float fromX, float toX)
{
	gTestNubs[0] = (OGLPoint3D) {fromX, 0, 0};
	gTestNubs[1] = (OGLPoint3D) {toX, 0, 0};
	gTestSectionVector = (OGLVector2D) {1, 0};
	gTestSectionNormal = (OGLVector2D) {-gTestSectionVector.y, gTestSectionVector.x};

	gTestFence = (FenceDefType)
	{
		.numNubs = 2,
		.nubList = gTestNubs,
		.bBox = {.min = {fromX, 0, 0}, .max = {toX, 0, 0}},
		.sectionVectors = &gTestSectionVector,
		.sectionNormals = &gTestSectionNormal,
	};
	gFenceList = &gTestFence;
	gNumFences = 1;
}

static void PlaceVehicle(ObjNode* vehicle, OGLPoint3D oldCoord, OGLPoint3D newCoord, OGLVector3D delta)
{
	memset(vehicle, 0, sizeof(*vehicle));
	vehicle->BoundingSphereRadius = RADIUS;
	vehicle->OldCoord = oldCoord;
	vehicle->Coord = newCoord;
	gCoord = newCoord;
	gDelta = delta;
}

static void TestReflectVector2D(void)
{
	OGLVector2D v = {3, 4};
	const OGLVector2D up = {0, 1};
	ReflectVector2D(&v, &up);									// bounces off the wall, keeping its length
	CHECK(fabsf(v.x - 3) < 0.001f && fabsf(v.y + 4) < 0.001f);

	v = (OGLVector2D) {0, 0};									// nothing to reflect: stays zero instead of NaN
	ReflectVector2D(&v, &up);
	CHECK(v.x == 0 && v.y == 0);

	v = (OGLVector2D) {-0.0f, 0.0f};
	ReflectVector2D(&v, &up);
	CHECK(v.x == 0 && v.y == 0);

	v = (OGLVector2D) {1e-30f, 0};								// squares underflow to a zero length
	ReflectVector2D(&v, &up);
	CHECK(isfinite(v.x) && isfinite(v.y));
	CHECK(fabsf(v.x) <= 1e-29f && fabsf(v.y) <= 1e-29f);
}

// A vehicle that isn't moving (friction brought a submarine's delta to exactly 0 while
// controls were off or it was immobilized) but whose sphere overlaps a fence is pushed out
// through the crossing branch. Reflecting its zero delta used to produce NaN, which then
// reached its camera and the terrain update's float->long supertile conversion.
static void TestStationaryVehicleOverlappingFence(void)
{
	ObjNode vehicle;
	const OGLPoint3D at = {5000, 300, 100};

	MakeFence(0, 10000);
	PlaceVehicle(&vehicle, at, at, (OGLVector3D) {0, -5, 0});
	DoFenceCollision(&vehicle);

	CHECK(isfinite(gDelta.x) && isfinite(gDelta.y) && isfinite(gDelta.z));
	CHECK(gDelta.x == 0 && gDelta.z == 0 && gDelta.y == -5);
	CHECK(fabsf(gCoord.x - at.x) < 1 && gCoord.z > RADIUS && gCoord.z < RADIUS + 10);	// pushed clear of the fence
}

// Same, through the /\ safety check: a fence endpoint lies inside the sphere.
static void TestStationaryVehicleAtFenceEnd(void)
{
	ObjNode vehicle;
	const OGLPoint3D at = {1100, 300, 0};

	MakeFence(0, 1000);
	PlaceVehicle(&vehicle, at, at, (OGLVector3D) {0, 0, 0});
	DoFenceCollision(&vehicle);

	CHECK(isfinite(gDelta.x) && isfinite(gDelta.z));
	CHECK(gDelta.x == 0 && gDelta.z == 0);
	CHECK(gCoord.x == at.x && gCoord.z == at.z);
}

// Seen on Atlantis with 6 cars at the start: a submarine wedged against another, nearly
// stationary one is thrust into a fence's radius, but SubHitSub restarts from its stored
// delta, so the delta reaching the fence is tiny enough that its squared length is 0.
static void TestWedgedSubmarineThrustIntoFence(void)
{
	ObjNode vehicle;

	MakeFence(0, 10000);
	PlaceVehicle(&vehicle, (OGLPoint3D) {5000, 300, 170}, (OGLPoint3D) {5009, 300, 140}, (OGLVector3D) {1.945124e-23f, 0, -1.89425141e-29f});
	DoFenceCollision(&vehicle);

	CHECK(isfinite(gDelta.x) && isfinite(gDelta.z));
	CHECK(fabsf(gDelta.x) <= 1e-22f && fabsf(gDelta.z) <= 1e-22f);
	CHECK(gCoord.z > RADIUS && gCoord.z < RADIUS + 10);
}

// A moving vehicle still bounces as before.
static void TestMovingVehicleBounces(void)
{
	ObjNode vehicle;

	MakeFence(0, 10000);
	PlaceVehicle(&vehicle, (OGLPoint3D) {5000, 300, 300}, (OGLPoint3D) {5000, 300, 100}, (OGLVector3D) {0, 0, -12000});
	DoFenceCollision(&vehicle);

	CHECK(fabsf(gDelta.x) < 0.01f && fabsf(gDelta.z - 12000 * .3f) < 0.5f);
	CHECK(gCoord.z > RADIUS && gCoord.z < RADIUS + 10);
}

int main(void)
{
	TestReflectVector2D();
	TestStationaryVehicleOverlappingFence();
	TestStationaryVehicleAtFenceEnd();
	TestWedgedSubmarineThrustIntoFence();
	TestMovingVehicleBounces();
	gFenceList = NULL;
	gNumFences = 0;
	puts("fence collision tests passed");
	return EXIT_SUCCESS;
}
