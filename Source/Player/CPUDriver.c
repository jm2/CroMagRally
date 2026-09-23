/****************************/
/*   	CPU DRIVER.C		    */
/****************************/


/****************************/
/*    EXTERNALS             */
/****************************/

#include "game.h"
#include "cpu_driver.h"


/******************** UPDATE CPU STUCK CHECK ***********************/
//
// Every POSITION_TIMER seconds, see how far the car got since the last check.
// If it barely moved, back up (with counter-steer) until the next check.
// If it is still stuck after backing up, drive forward again.
//

void UpdateCPUStuckCheck(PlayerInfoType *pinfo, const OGLPoint3D *coord, float dt)
{
float	stuckDist;

	pinfo->oldPositionTimer -= dt;
	if (!(pinfo->oldPositionTimer <= 0.0f))									// see if time to do the check
		return;

	pinfo->oldPositionTimer += POSITION_TIMER;								// reset timer

	if (pinfo->onWater)
		stuckDist = 40.0f;
	else
		stuckDist = 80.0f;

	if (CalcDistance3D(pinfo->oldPosition.x, pinfo->oldPosition.y, pinfo->oldPosition.z,
						coord->x, coord->y, coord->z) < stuckDist)			// see if player isnt moving
	{
		if (pinfo->reverseTimer > 0.0f)										// if was reversing then go forward again
			pinfo->reverseTimer = 0;
		else
			pinfo->reverseTimer = 4.0f;										// try moving backwards to get unstuck
	}
	else
	{
		pinfo->reverseTimer = 0;											// player is NOT stuck, so go forward
	}

	pinfo->oldPosition = *coord;											// remember position
}


/******************** CPU SHOULD BRAKE FOR SKID ***********************/
//
// Brake if sliding or spinning.
//
// Planing and greased tires only matter for grip on the ground, and DoCarMotion
// only expires them there. A car that slides into deep water keeps them until it
// is back on land, so braking for them afloat would leave the CPU sitting in the
// water until another car pushed it ashore.
//

Boolean CPUShouldBrakeForSkid(const PlayerInfoType *pinfo, Boolean onGround, float spinRate)
{
	if (fabs(spinRate) > PI)												// spinning out
		return true;

	if (!onGround)															// afloat: planing is stale, keep driving
		return false;

	return pinfo->isPlaning || (pinfo->greasedTiresTimer != 0.0f);
}


/******************** CPU PEDAL CONTROL BITS ***********************/
//
// Brake beats gas. Gas goes backwards while the stuck check has us reversing.
//

uint32_t CPUPedalControlBits(PlayerInfoType *pinfo, Boolean brake, Boolean giveGas, float dt)
{
	if (brake)
		return (uint32_t) 1 << kControlBit_Brakes;

	if (!giveGas)
		return 0;

	if (pinfo->reverseTimer > 0.0f)											// see if going in reverse
	{
		if ((pinfo->reverseTimer -= dt) < 0.0f)								// dec reverse timer
			pinfo->reverseTimer = 0;
		return (uint32_t) 1 << kControlBit_Backward;
	}

	return (uint32_t) 1 << kControlBit_Forward;								// go forward
}


/******************** CPU RACE PROGRESS ***********************/
//
// Laps and checkpoints crossed, as one number that grows as the car goes forward.
// A race starts at lap -1, checkpoint N-1: progress -1.
//

int CPURaceProgress(short lapNum, short checkpointNum, long numCheckpoints)
{
	return (int) lapNum * (int) numCheckpoints + (int) checkpointNum;
}


/******************** UPDATE CPU RESCUE TIMER ***********************/
//
// Counts the time since the car last reached a new best progress: a checkpoint further
// on, or CPU_RESCUE_MIN_GAIN closer to the next checkpoint than its best since the last
// one. Returns true, and restarts the count, once it has gone CPU_RESCUE_TIME without
// either. Driving backward or losing a lap is no progress. While not racing (starting
// lights, race over) the count stays at zero.
//

Boolean UpdateCPURescueTimer(PlayerInfoType *pinfo, int progress, float distToNext, Boolean racing, float dt)
{
	if (progress > pinfo->rescueProgress)
	{
		pinfo->rescueProgress = progress;
		pinfo->rescueBestDist = distToNext;
		pinfo->rescueTimer = 0;
		return false;
	}

	if (progress == pinfo->rescueProgress && distToNext <= pinfo->rescueBestDist - CPU_RESCUE_MIN_GAIN)
	{
		pinfo->rescueBestDist = distToNext;
		pinfo->rescueTimer = 0;
		return false;
	}

	if (!racing)
	{
		pinfo->rescueTimer = 0;
		return false;
	}

	pinfo->rescueTimer += dt;
	if (pinfo->rescueTimer < CPU_RESCUE_TIME)
		return false;

	pinfo->rescueTimer = 0;
	return true;
}


/******************** RECORD CPU RESCUE CROSSING ***********************/
//
// Where the car just crossed a checkpoint going forward, and which way it was driving.
// A zero-length move keeps the previous direction.
//

void RecordCPURescueCrossing(PlayerInfoType *pinfo, float x, float z, float dirX, float dirZ)
{
	const float length = sqrtf(dirX * dirX + dirZ * dirZ);

	pinfo->rescueX = x;
	pinfo->rescueZ = z;
	if (length > 0.0f && isfinite(length))
	{
		pinfo->rescueDirX = dirX / length;
		pinfo->rescueDirZ = dirZ / length;
	}
}


/******************** FIND CPU RESCUE SPOT ***********************/
//
// CPU_RESCUE_AHEAD past the car's last forward checkpoint crossing, facing the way it
// was driving then. If another car is within CPU_RESCUE_CLEARANCE, further along that
// way, a CPU_RESCUE_STEP at a time (twice at most); if all are taken, the first. Only
// simulation state, so every network peer agrees.
//

CPURescueSpot FindCPURescueSpot(const PlayerInfoType *pinfo, const OGLPoint3D others[], int numOthers)
{
	const float dx = pinfo->rescueDirX, dz = pinfo->rescueDirZ;
	CPURescueSpot spot = { pinfo->rescueX + dx * CPU_RESCUE_AHEAD, pinfo->rescueZ + dz * CPU_RESCUE_AHEAD, atan2f(-dx, -dz) };

	for (int step = 0; step <= 2; step++)
	{
		const float x = pinfo->rescueX + dx * (CPU_RESCUE_AHEAD + CPU_RESCUE_STEP * (float) step);
		const float z = pinfo->rescueZ + dz * (CPU_RESCUE_AHEAD + CPU_RESCUE_STEP * (float) step);
		Boolean clear = true;

		for (int j = 0; j < numOthers && clear; j++)
		{
			const float ox = others[j].x - x, oz = others[j].z - z;
			clear = ox * ox + oz * oz >= CPU_RESCUE_CLEARANCE * CPU_RESCUE_CLEARANCE;
		}
		if (clear)
		{
			spot.x = x;
			spot.z = z;
			break;
		}
	}

	return spot;
}
