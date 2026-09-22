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

// The best land car type unlocked after agesCompleted ages.
int GetBestUnlockedLandCarType(int agesCompleted);

// Vehicle for the cpuIndex-th CPU entrant (0-based, in player order). A pick does not
// depend on how many CPUs follow it, so callers may pick lazily between other synced
// RNG draws. The result is always within 0...GetBestUnlockedLandCarType(agesCompleted).
int PickCPUVehicle(const CPUVehiclePickRules* rules, int cpuIndex);
