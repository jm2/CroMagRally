/****************************/
/*   	FINITE GUARD.C	    */
/****************************/

#include "game.h"
#include "finite_guard.h"
#include <math.h>

_Static_assert(MAX_PLAYERS <= 32, "FirstNonFiniteReport keeps one bit per player in a uint32_t");


/******************** IS FINITE POINT/VECTOR 3D ************************/

Boolean IsFinitePoint3D(const OGLPoint3D *p)
{
	return isfinite(p->x) && isfinite(p->y) && isfinite(p->z);
}

Boolean IsFiniteVector3D(const OGLVector3D *v)
{
	return isfinite(v->x) && isfinite(v->y) && isfinite(v->z);
}


/******************** GET VEHICLE MOTION STATE ************************/

VehicleMotionState GetVehicleMotionState(const ObjNode *vehicle, float rpm)
{
	VehicleMotionState	state =
	{
		.coord		= vehicle->Coord,
		.delta		= vehicle->Delta,
		.speed2D	= vehicle->Speed2D,
		.speed3D	= vehicle->Speed3D,
		.rot		= vehicle->Rot,
		.deltaRot	= vehicle->DeltaRot,
		.rpm		= rpm,
	};

	return(state);
}


/******************** REPAIR VEHICLE MOTION ************************/
//
// Checks a vehicle's motion state at the end of its move and returns a mask of the
// kNonFinite_ fields that were NaN or infinite (0 == finite, left untouched).
//
// A non-finite position or angle goes back to its value in lastFinite (the state at the
// start of the frame), or to 0 if that is not finite either. Rates are not replayed:
// the velocity that just produced a bad state is zeroed, as is the spin when the angle
// had to be restored, so the vehicle simply stops for a frame and physics resumes.
//

uint32_t RepairVehicleMotion(VehicleMotionState *state, const VehicleMotionState *lastFinite)
{
uint32_t	fields = 0;

	if (!IsFinitePoint3D(&state->coord))
		fields |= kNonFinite_Coord;
	if (!IsFiniteVector3D(&state->delta))
		fields |= kNonFinite_Delta;
	if (!isfinite(state->speed2D) || !isfinite(state->speed3D))
		fields |= kNonFinite_Speed;
	if (!IsFiniteVector3D(&state->rot))
		fields |= kNonFinite_Rot;
	if (!IsFiniteVector3D(&state->deltaRot))
		fields |= kNonFinite_DeltaRot;
	if (!isfinite(state->rpm))
		fields |= kNonFinite_RPM;

	if (fields == 0)
		return(0);

	if (fields & kNonFinite_Coord)
	{
		if (IsFinitePoint3D(&lastFinite->coord))
			state->coord = lastFinite->coord;
		else
			state->coord = (OGLPoint3D) {0, 0, 0};
	}

	if (fields & kNonFinite_Rot)
	{
		if (IsFiniteVector3D(&lastFinite->rot))
			state->rot = lastFinite->rot;
		else
			state->rot = (OGLVector3D) {0, 0, 0};
	}

	if (fields & (kNonFinite_Coord | kNonFinite_Delta | kNonFinite_Speed))
	{
		state->delta = (OGLVector3D) {0, 0, 0};
		state->speed2D = 0;
		state->speed3D = 0;
		state->rpm = 0;
	}

	if (fields & (kNonFinite_Rot | kNonFinite_DeltaRot))
		state->deltaRot = (OGLVector3D) {0, 0, 0};

	if (fields & kNonFinite_RPM)
		state->rpm = 0;

	return(fields);
}


/******************** KEEP CAMERA PLACEMENT FINITE ************************/
//
// Returns true when from/to are finite. Otherwise restores both from the last
// placement (or 0 if that is not finite either) and returns false.
//

Boolean KeepCameraPlacementFinite(OGLPoint3D *from, OGLPoint3D *to, const OGLPoint3D *lastFrom, const OGLPoint3D *lastTo)
{
	if (IsFinitePoint3D(from) && IsFinitePoint3D(to))
		return(true);

	*from = IsFinitePoint3D(lastFrom) ? *lastFrom : (OGLPoint3D) {0, 0, 0};
	*to = IsFinitePoint3D(lastTo) ? *lastTo : (OGLPoint3D) {0, 0, 0};
	return(false);
}


/******************** FIRST NON-FINITE REPORT ************************/
//
// Returns true the first time a call site sees a non-finite value for this player,
// so it logs once instead of every frame.
//

Boolean FirstNonFiniteReport(uint32_t *reportedPlayers, int playerNum)
{
uint32_t	bit;

	if (playerNum < 0 || playerNum >= 32)
		return(false);

	bit = UINT32_C(1) << playerNum;
	if (*reportedPlayers & bit)
		return(false);

	*reportedPlayers |= bit;
	return(true);
}


/******************** DESCRIBE NON-FINITE FIELDS ************************/

void DescribeNonFiniteFields(uint32_t fields, char *buffer, size_t bufferSize)
{
static const struct
{
	uint32_t	bit;
	const char	*name;
} kNames[] =
{
	{ kNonFinite_Coord,		"coord" },
	{ kNonFinite_Delta,		"delta" },
	{ kNonFinite_Speed,		"speed" },
	{ kNonFinite_Rot,		"rot" },
	{ kNonFinite_DeltaRot,	"deltaRot" },
	{ kNonFinite_RPM,		"rpm" },
};

	if (bufferSize == 0)
		return;

	buffer[0] = '\0';
	for (size_t i = 0; i < SDL_arraysize(kNames); i++)
	{
		if (fields & kNames[i].bit)
		{
			if (buffer[0] != '\0')
				SDL_strlcat(buffer, ",", bufferSize);
			SDL_strlcat(buffer, kNames[i].name, bufferSize);
		}
	}
}


/******************** LOG NON-FINITE VEHICLE ************************/

void LogNonFiniteVehicle(const char *kind, int playerNum, uint32_t frame, uint32_t fields,
						const VehicleMotionState *bad, const VehicleMotionState *repaired)
{
char	names[64];

	DescribeNonFiniteFields(fields, names, sizeof(names));
	SDL_Log("Non-finite %s state for player %d at frame %u (%s): coord (%g, %g, %g) delta (%g, %g, %g) rot (%g, %g, %g) rpm %g;"
			" now coord (%g, %g, %g) delta (%g, %g, %g) rot (%g, %g, %g) rpm %g",
			kind, playerNum, (unsigned) frame, names,
			bad->coord.x, bad->coord.y, bad->coord.z, bad->delta.x, bad->delta.y, bad->delta.z,
			bad->rot.x, bad->rot.y, bad->rot.z, bad->rpm,
			repaired->coord.x, repaired->coord.y, repaired->coord.z, repaired->delta.x, repaired->delta.y, repaired->delta.z,
			repaired->rot.x, repaired->rot.y, repaired->rot.z, repaired->rpm);
}
