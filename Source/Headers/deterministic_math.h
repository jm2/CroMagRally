#pragma once

#include <stdbool.h>
#include <stdint.h>

enum DeterministicEventTag
{
	kDeterministicEvent_LandMineHit = 0x4D494E45,	// MINE
	kDeterministicEvent_CampfireHit = 0x46495245,	// FIRE
	kDeterministicEvent_FlagPlace = 0x464C4147,	// FLAG
	kDeterministicEvent_CatapultThrow = 0x43415441,	// CATA
	kDeterministicEvent_GoddessBolt = 0x474F4453,	// GODS
	kDeterministicEvent_CannonShot = 0x43414E4E,	// CANN
	kDeterministicEvent_SubStuck = 0x53554253,	// SUBS
	kDeterministicEvent_CarCollision = 0x43415253,	// CARS
};

uint32_t DeterministicPairKey(uint32_t first, uint32_t second);
uint32_t DeterministicUnorderedPairKey(uint32_t first, uint32_t second);
uint32_t DeterministicFrameKey(bool networkGame, uint32_t hostSendCounter, uint32_t simulationFrame);
float DeterministicEventFloat(uint32_t frame, uint32_t eventTag, uint32_t entityKey, uint32_t sampleIndex);
float DeterministicStableFloat(uint32_t eventTag, uint32_t entityKey, uint32_t sampleIndex);
