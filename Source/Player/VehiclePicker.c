#include "game.h"
#include "vehicle_picker.h"

#define	NUM_STARTER_CAR_TYPES	6						// cars available before completing any age
#define	CARS_UNLOCKED_PER_AGE	2
#define	MAX_UNLOCKING_AGES		2						// winning the last age unlocks nothing more

_Static_assert(NUM_LAND_CAR_TYPES <= 32, "humanCarMask holds one bit per land car type");
_Static_assert(NUM_STARTER_CAR_TYPES + MAX_UNLOCKING_AGES * CARS_UNLOCKED_PER_AGE == NUM_LAND_CAR_TYPES,
	"completing every age unlocks the whole land roster");


/****************** GET BEST UNLOCKED LAND CAR TYPE *********************/

int GetBestUnlockedLandCarType(int agesCompleted)
{
	if (agesCompleted < 0)
		agesCompleted = 0;
	if (agesCompleted > MAX_UNLOCKING_AGES)						// dont get extra cars after winning, so pin @ 2
		agesCompleted = MAX_UNLOCKING_AGES;

	return NUM_STARTER_CAR_TYPES + agesCompleted * CARS_UNLOCKED_PER_AGE - 1;
}


/************************ PICK CPU VEHICLE ***************************/
//
// Hard mode draws any unlocked car, so duplicates are allowed. Other difficulties take
// the best unlocked cars that no human drives, one each. When the CPUs outnumber those
// cars, the same cars are handed out again, best first; humans' cars are only used
// when the humans hold every unlocked car.
//

int PickCPUVehicle(const CPUVehiclePickRules* rules, int cpuIndex)
{
int	freeTypes[NUM_LAND_CAR_TYPES];
int	numFree = 0;
const int best = GetBestUnlockedLandCarType(rules->agesCompleted);

	if (cpuIndex < 0)
		cpuIndex = 0;

	if (rules->difficulty == DIFFICULTY_HARD && rules->randomRange)
	{
		int type = rules->randomRange(rules->randomContext, cpuIndex, 0, (uint16_t) best);
		return type <= best ? type : best;
	}

	for (int type = best; type >= 0; type--)						// start @ end of usable cars so it will pick best cars
	{
		if (!(rules->humanCarMask & (1u << type)))					// skip over vehicles already used by Humans
			freeTypes[numFree++] = type;
	}

	if (numFree > 0)
		return freeTypes[cpuIndex % numFree];

	return best - cpuIndex % (best + 1);
}


/******************** SHARED CPU VEHICLE RANDOM *********************/
//
// Hard mode's draw for a network CPU. A stateless function of the seed and the slot:
// it neither consumes the synced RNG nor depends on the order peers pick in. The
// draw is scaled in integers, so every platform rounds it alike.
//

static uint16_t SharedCPUVehicleRandom(void* context, int cpuIndex, uint16_t min, uint16_t max)
{
	const SharedCPUVehicleSeed* seed = (const SharedCPUVehicleSeed*) context;
	const uint32_t slot = (uint32_t) (seed->firstCPUSlot + cpuIndex);
	const uint32_t draw = (uint32_t) (DeterministicStableFloat(kDeterministicEvent_CpuVehicle, seed->key, slot)
										* 16777216.0f);					// the float's 24-bit integer, exactly
	const uint32_t range = (uint32_t) max - min + 1u;

	return (uint16_t) (min + (uint32_t) (((uint64_t) draw * range) >> 24));
}


/******************** INIT SHARED CPU VEHICLE PICK RULES *********************/

void InitSharedCPUVehiclePickRules(CPUVehiclePickRules* rules, SharedCPUVehicleSeed* seed,
		const short humanCars[], int numHumans, int difficulty, int trackNum)
{
	seed->key = DeterministicPairKey((uint32_t) trackNum, (uint32_t) numHumans);
	seed->firstCPUSlot = numHumans;

	rules->humanCarMask = 0;
	for (int h = 0; h < numHumans; h++)
	{
		if (humanCars[h] >= 0 && humanCars[h] < NUM_LAND_CAR_TYPES)
			rules->humanCarMask |= 1u << humanCars[h];
		seed->key = DeterministicPairKey(seed->key, (uint32_t) humanCars[h]);
	}

	rules->agesCompleted = MAX_UNLOCKING_AGES;							// the whole roster, whatever this machine unlocked
	rules->difficulty = difficulty;
	rules->randomRange = SharedCPUVehicleRandom;
	rules->randomContext = seed;
}
