#include "game.h"
#include "finite_guard.h"
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) do { if (!(condition)) { \
	fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); \
	exit(EXIT_FAILURE); } } while (0)

static VehicleMotionState MakeState(float base)
{
	VehicleMotionState s =
	{
		.coord = {base + 1, base + 2, base + 3},
		.delta = {base + 4, base + 5, base + 6},
		.speed2D = base + 7,
		.speed3D = base + 8,
		.rot = {base + 9, base + 10, base + 11},
		.deltaRot = {base + 12, base + 13, base + 14},
		.rpm = base + 15,
	};
	return s;
}

static Boolean StateIsFinite(const VehicleMotionState* s)
{
	return IsFinitePoint3D(&s->coord) && IsFiniteVector3D(&s->delta)
		&& isfinite(s->speed2D) && isfinite(s->speed3D)
		&& IsFiniteVector3D(&s->rot) && IsFiniteVector3D(&s->deltaRot) && isfinite(s->rpm);
}

static Boolean SamePoint(OGLPoint3D a, OGLPoint3D b) { return a.x == b.x && a.y == b.y && a.z == b.z; }
static Boolean SameVector(OGLVector3D a, OGLVector3D b) { return a.x == b.x && a.y == b.y && a.z == b.z; }
static Boolean IsZero(OGLVector3D v) { return v.x == 0 && v.y == 0 && v.z == 0; }

static void TestFiniteStateIsUntouched(void)
{
	const VehicleMotionState last = MakeState(100);
	VehicleMotionState s = MakeState(0);
	const VehicleMotionState before = s;
	CHECK(RepairVehicleMotion(&s, &last) == 0);
	CHECK(memcmp(&s, &before, sizeof(s)) == 0);

	s.coord.x = -FLT_MAX;										// huge but finite values are not this guard's business
	s.delta.z = FLT_MAX;
	CHECK(RepairVehicleMotion(&s, &last) == 0);
	CHECK(s.coord.x == -FLT_MAX && s.delta.z == FLT_MAX);
}

static void TestPositionIsRestoredAndVehicleStopped(void)
{
	const VehicleMotionState last = MakeState(100);
	VehicleMotionState s = MakeState(0);
	s.coord.z = NAN;
	s.delta.x = NAN;
	s.speed2D = NAN;
	CHECK(RepairVehicleMotion(&s, &last) == (kNonFinite_Coord | kNonFinite_Delta | kNonFinite_Speed));
	CHECK(SamePoint(s.coord, last.coord));
	CHECK(IsZero(s.delta));
	CHECK(s.speed2D == 0 && s.speed3D == 0 && s.rpm == 0);
	CHECK(SameVector(s.rot, MakeState(0).rot));					// the angle was fine, so it and the spin are kept
	CHECK(SameVector(s.deltaRot, MakeState(0).deltaRot));

	s = MakeState(0);
	s.delta.y = INFINITY;										// a bad velocity alone stops the vehicle where it is
	CHECK(RepairVehicleMotion(&s, &last) == kNonFinite_Delta);
	CHECK(SamePoint(s.coord, MakeState(0).coord));
	CHECK(IsZero(s.delta) && s.speed2D == 0 && s.speed3D == 0 && s.rpm == 0);

	s = MakeState(0);
	s.speed3D = -INFINITY;
	CHECK(RepairVehicleMotion(&s, &last) == kNonFinite_Speed);
	CHECK(IsZero(s.delta) && s.speed3D == 0);
}

static void TestAngleIsRestoredAndSpinStopped(void)
{
	const VehicleMotionState last = MakeState(100);
	VehicleMotionState s = MakeState(0);
	s.rot.y = NAN;
	CHECK(RepairVehicleMotion(&s, &last) == kNonFinite_Rot);
	CHECK(SameVector(s.rot, last.rot));
	CHECK(IsZero(s.deltaRot));
	CHECK(SameVector(s.delta, MakeState(0).delta));				// the velocity was fine
	CHECK(SamePoint(s.coord, MakeState(0).coord));

	s = MakeState(0);
	s.deltaRot.z = NAN;
	CHECK(RepairVehicleMotion(&s, &last) == kNonFinite_DeltaRot);
	CHECK(IsZero(s.deltaRot));
	CHECK(SameVector(s.rot, MakeState(0).rot));

	s = MakeState(0);
	s.rpm = NAN;												// a submarine's speed lives in its RPM
	CHECK(RepairVehicleMotion(&s, &last) == kNonFinite_RPM);
	CHECK(s.rpm == 0);
	CHECK(SameVector(s.delta, MakeState(0).delta));
}

static void TestNonFiniteFallbackBecomesZero(void)
{
	VehicleMotionState last = MakeState(100);
	last.coord.y = NAN;
	last.rot.x = INFINITY;
	VehicleMotionState s = MakeState(0);
	s.coord.x = NAN;
	s.rot.z = NAN;
	CHECK(RepairVehicleMotion(&s, &last) == (kNonFinite_Coord | kNonFinite_Rot));
	CHECK(SamePoint(s.coord, (OGLPoint3D) {0, 0, 0}));
	CHECK(IsZero(s.rot));
	CHECK(StateIsFinite(&s));
}

static void TestEveryCombinationEndsFinite(void)
{
	const float kBad[] = {NAN, INFINITY, -INFINITY};	// not static: some MSVC SDKs define NAN as a function call
	const VehicleMotionState last = MakeState(100);

	for (int bad = 0; bad < 3; bad++)
	{
		for (uint32_t mask = 0; mask < 64; mask++)
		{
			VehicleMotionState s = MakeState(0);
			const float v = kBad[bad];
			if (mask & kNonFinite_Coord)	s.coord.y = v;
			if (mask & kNonFinite_Delta)	s.delta.z = v;
			if (mask & kNonFinite_Speed)	s.speed2D = v;
			if (mask & kNonFinite_Rot)		s.rot.x = v;
			if (mask & kNonFinite_DeltaRot)	s.deltaRot.y = v;
			if (mask & kNonFinite_RPM)		s.rpm = v;
			CHECK(RepairVehicleMotion(&s, &last) == mask);
			CHECK(StateIsFinite(&s));
			CHECK(RepairVehicleMotion(&s, &last) == 0);			// idempotent once repaired
		}
	}
}

static void TestGetVehicleMotionState(void)
{
	ObjNode node;
	memset(&node, 0, sizeof(node));
	node.Coord = (OGLPoint3D) {1, 2, 3};
	node.Delta = (OGLVector3D) {4, 5, 6};
	node.Speed2D = 7;
	node.Speed3D = 8;
	node.Rot = (OGLVector3D) {9, 10, 11};
	node.DeltaRot = (OGLVector3D) {12, 13, 14};
	const VehicleMotionState s = GetVehicleMotionState(&node, 15);
	const VehicleMotionState expected = MakeState(0);
	CHECK(memcmp(&s, &expected, sizeof(s)) == 0);
}

static void TestCameraPlacement(void)
{
	const OGLPoint3D lastFrom = {10, 20, 30}, lastTo = {40, 50, 60};
	OGLPoint3D from = {1, 2, 3}, to = {4, 5, 6};

	CHECK(KeepCameraPlacementFinite(&from, &to, &lastFrom, &lastTo));
	CHECK(SamePoint(from, (OGLPoint3D) {1, 2, 3}) && SamePoint(to, (OGLPoint3D) {4, 5, 6}));

	from.z = NAN;												// the terrain streamer truncates from.x/z to supertile indices
	CHECK(!KeepCameraPlacementFinite(&from, &to, &lastFrom, &lastTo));
	CHECK(SamePoint(from, lastFrom) && SamePoint(to, lastTo));

	to.x = -INFINITY;
	CHECK(!KeepCameraPlacementFinite(&from, &to, &lastFrom, &lastTo));
	CHECK(SamePoint(from, lastFrom) && SamePoint(to, lastTo));

	const OGLPoint3D badLast = {NAN, 0, 0};
	from.x = NAN;
	CHECK(!KeepCameraPlacementFinite(&from, &to, &badLast, &lastTo));
	CHECK(SamePoint(from, (OGLPoint3D) {0, 0, 0}) && SamePoint(to, lastTo));
}

static void TestReportOncePerPlayer(void)
{
	uint32_t reported = 0;
	CHECK(FirstNonFiniteReport(&reported, 0));
	CHECK(!FirstNonFiniteReport(&reported, 0));
	CHECK(FirstNonFiniteReport(&reported, MAX_PLAYERS - 1));
	CHECK(!FirstNonFiniteReport(&reported, MAX_PLAYERS - 1));
	CHECK(FirstNonFiniteReport(&reported, 31));
	CHECK(!FirstNonFiniteReport(&reported, -1));
	CHECK(!FirstNonFiniteReport(&reported, 32));
	CHECK(reported == (1u | (1u << (MAX_PLAYERS - 1)) | (1u << 31)));
}

static void TestDescribeFields(void)
{
	char buffer[64];
	DescribeNonFiniteFields(0, buffer, sizeof(buffer));
	CHECK(strcmp(buffer, "") == 0);
	DescribeNonFiniteFields(kNonFinite_Coord | kNonFinite_Delta, buffer, sizeof(buffer));
	CHECK(strcmp(buffer, "coord,delta") == 0);
	DescribeNonFiniteFields(0x3F, buffer, sizeof(buffer));
	CHECK(strcmp(buffer, "coord,delta,speed,rot,deltaRot,rpm") == 0);

	char tiny[4] = {'x', 'x', 'x', 'x'};
	DescribeNonFiniteFields(kNonFinite_Coord, tiny, sizeof(tiny));
	CHECK(strcmp(tiny, "coo") == 0);
	tiny[0] = 'x';
	DescribeNonFiniteFields(kNonFinite_Coord, tiny, 0);
	CHECK(tiny[0] == 'x');

	VehicleMotionState bad = MakeState(0), repaired = MakeState(0);
	bad.coord.x = NAN;
	LogNonFiniteVehicle("car", 3, 1234, kNonFinite_Coord, &bad, &repaired);	// must format NaN without trouble
}

int main(void)
{
	TestFiniteStateIsUntouched();
	TestPositionIsRestoredAndVehicleStopped();
	TestAngleIsRestoredAndSpinStopped();
	TestNonFiniteFallbackBecomesZero();
	TestEveryCombinationEndsFinite();
	TestGetVehicleMotionState();
	TestCameraPlacement();
	TestReportOncePerPlayer();
	TestDescribeFields();
	puts("finite guard tests passed");
	return EXIT_SUCCESS;
}
