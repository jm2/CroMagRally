//
// cpu_driver.h
//
// Pedal and stuck-recovery decisions of the CPU car driver (DoCPUControl_Car).
// They only read simulation state and advance with the simulation dt, so every
// peer makes the same choices for network bots.
//

#pragma once

void UpdateCPUStuckCheck(PlayerInfoType *pinfo, const OGLPoint3D *coord, float dt);
Boolean CPUShouldBrakeForSkid(const PlayerInfoType *pinfo, Boolean onGround, float spinRate);
uint32_t CPUPedalControlBits(PlayerInfoType *pinfo, Boolean brake, Boolean giveGas, float dt);

// CPU rescue. A car can land on or behind a fence after a jump or a knock, where
// path-following steers it into the fence for good. A CPU-driven car that makes no
// forward race progress for CPU_RESCUE_TIME seconds is put back where it last crossed a
// checkpoint going forward (a spot it has already driven through, so on the road),
// facing the way it was driving then. Progress is a new checkpoint, or getting at least
// CPU_RESCUE_MIN_GAIN closer to the next one than ever before, so a car that is slowly
// working its way forward (e.g. across a river) is left alone.
#define	CPU_RESCUE_TIME			20.0f		// seconds without forward race progress
#define	CPU_RESCUE_MIN_GAIN		500.0f		// closer to the next checkpoint than the best so far
#define	CPU_RESCUE_NO_DIST		1e30f		// rescueBestDist before any distance is measured
#define	CPU_RESCUE_AHEAD		200.0f		// how far past the crossing point to place the car
#define	CPU_RESCUE_STEP			500.0f		// further along the way, if another car is there
#define	CPU_RESCUE_CLEARANCE	500.0f		// keep this far from every other car

typedef struct
{
	float	x, z;
	float	rotY;							// facing the way the car crossed the checkpoint
}CPURescueSpot;

int CPURaceProgress(short lapNum, short checkpointNum, long numCheckpoints);
Boolean UpdateCPURescueTimer(PlayerInfoType *pinfo, int progress, float distToNext, Boolean racing, float dt);
void RecordCPURescueCrossing(PlayerInfoType *pinfo, float x, float z, float dirX, float dirZ);
Boolean FindCPURescueSpot(const PlayerInfoType *pinfo, const OGLPoint3D others[], int numOthers, CPURescueSpot *spot);
