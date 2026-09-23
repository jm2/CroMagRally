#pragma once

#include <stdint.h>

// CPU vehicle choice, kept free of game state so it can be tested exhaustively and
// reused with a deterministic random source.

// Returns a value in the inclusive range [min, max], like RandomRange. cpuIndex is
// the pick being made, so a stateless source can use it as a key.
typedef uint16_t (*CPUVehicleRandomRangeProc)(void* context, int cpuIndex, uint16_t min, uint16_t max);

typedef struct
{
	uint32_t					humanCarMask;		// bit t set: a human drives land car type t
	int							agesCompleted;		// tournament progress; pinned to 0...2
	int							difficulty;			// DIFFICULTY_*
	CPUVehicleRandomRangeProc	randomRange;		// called once per pick, and only on DIFFICULTY_HARD
	void*						randomContext;
} CPUVehiclePickRules;

// What a network game's CPU picks may depend on: only state every peer shares.
typedef struct
{
	uint32_t					key;				// the track and the humans' cars
	int							firstCPUSlot;		// player slot of the CPU with cpuIndex 0
} SharedCPUVehicleSeed;

// The best land car type unlocked after agesCompleted ages.
int GetBestUnlockedLandCarType(int agesCompleted);

// Vehicle for the cpuIndex-th CPU entrant (0-based, in player order). A pick does not
// depend on how many CPUs follow it, so callers may pick lazily between other synced
// RNG draws. The result is always within 0...GetBestUnlockedLandCarType(agesCompleted).
int PickCPUVehicle(const CPUVehiclePickRules* rules, int cpuIndex);

// Rules for the CPU cars that fill a network race, which every peer must pick alike:
// the humans' cars (humanCars[0...numHumans-1], slots 0...numHumans-1, including players
// who left since), the whole land roster whatever this machine has unlocked, and on
// DIFFICULTY_HARD a stateless draw keyed on the track, the humans' cars and the CPU's
// slot instead of the synced RNG. CPU cpuIndex sits in slot numHumans + cpuIndex.
// seed must outlive rules.
void InitSharedCPUVehiclePickRules(CPUVehiclePickRules* rules, SharedCPUVehicleSeed* seed,
		const short humanCars[], int numHumans, int difficulty, int trackNum);
