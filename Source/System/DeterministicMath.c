#include "deterministic_math.h"

static uint32_t Avalanche32(uint32_t value)
{
	value ^= value >> 16;
	value *= 0x7FEB352Du;
	value ^= value >> 15;
	value *= 0x846CA68Bu;
	value ^= value >> 16;
	return value;
}

uint32_t DeterministicPairKey(uint32_t first, uint32_t second)
{
	uint32_t value = Avalanche32(first + 0x9E3779B9u);
	value ^= Avalanche32(second + 0x85EBCA6Bu);
	return Avalanche32(value);
}

uint32_t DeterministicUnorderedPairKey(uint32_t first, uint32_t second)
{
	return first <= second
		? DeterministicPairKey(first, second)
		: DeterministicPairKey(second, first);
}

uint32_t DeterministicFrameKey(bool networkGame, uint32_t hostSendCounter, uint32_t simulationFrame)
{
	return networkGame && hostSendCounter > 0
		? hostSendCounter - 1
		: simulationFrame;
}

// Stateless event jitter for simulation-sensitive choices. Every input is an integer with
// identical meaning on every peer: never feed this mutable coordinates, speeds, or timers.
float DeterministicEventFloat(uint32_t frame, uint32_t eventTag, uint32_t entityKey, uint32_t sampleIndex)
{
	uint32_t value = DeterministicPairKey(frame, eventTag);
	value = DeterministicPairKey(value, entityKey);
	value = DeterministicPairKey(value, sampleIndex);
	return (value >> 8) * 0x1.0p-24f;
}

float DeterministicStableFloat(uint32_t eventTag, uint32_t entityKey, uint32_t sampleIndex)
{
	return DeterministicEventFloat(0, eventTag, entityKey, sampleIndex);
}
