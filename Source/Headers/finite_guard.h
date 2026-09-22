#pragma once

//
// NaN/infinity guards for vehicle and camera state.
//
// Vehicle and camera positions are later truncated to integer supertile and tile
// indices (e.g. DoPlayerTerrainUpdate), and a NaN velocity or camera angle never
// recovers on its own once it is fed back into the next frame. These helpers let
// the code that produces that state detect a non-finite value, restore the last
// finite one and report it. They are pure and deterministic, so every peer makes
// the same repair from the same state.
//

enum
{
	kNonFinite_Coord		= 1 << 0,
	kNonFinite_Delta		= 1 << 1,
	kNonFinite_Speed		= 1 << 2,
	kNonFinite_Rot			= 1 << 3,
	kNonFinite_DeltaRot		= 1 << 4,
	kNonFinite_RPM			= 1 << 5,
};

typedef struct
{
	OGLPoint3D		coord;
	OGLVector3D		delta;
	float			speed2D, speed3D;
	OGLVector3D		rot;
	OGLVector3D		deltaRot;
	float			rpm;					// submarines store their speed here
}VehicleMotionState;

Boolean IsFinitePoint3D(const OGLPoint3D *p);
Boolean IsFiniteVector3D(const OGLVector3D *v);

VehicleMotionState GetVehicleMotionState(const ObjNode *vehicle, float rpm);
uint32_t RepairVehicleMotion(VehicleMotionState *state, const VehicleMotionState *lastFinite);
Boolean KeepCameraPlacementFinite(OGLPoint3D *from, OGLPoint3D *to, const OGLPoint3D *lastFrom, const OGLPoint3D *lastTo);

Boolean FirstNonFiniteReport(uint32_t *reportedPlayers, int playerNum);
void DescribeNonFiniteFields(uint32_t fields, char *buffer, size_t bufferSize);
void LogNonFiniteVehicle(const char *kind, int playerNum, uint32_t frame, uint32_t fields,
						const VehicleMotionState *bad, const VehicleMotionState *repaired);
