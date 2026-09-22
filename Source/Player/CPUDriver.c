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
