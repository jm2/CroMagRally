#include "RNG.h"
#include <time.h>

// Global Instances
pcg32_random_t gSimRNG;
pcg32_random_t gLocalRNG;

// Core PCG Implementation
// Source: PCG-Basic C Implementation (pcg-random.org)
// Minimal C99 Implementation

void pcg32_srandom_r(pcg32_random_t* rng, uint64_t initstate, uint64_t initseq)
{
    rng->state = 0U;
    rng->inc = (initseq << 1u) | 1u;
    pcg32_random_r(rng);
    rng->state += initstate;
    pcg32_random_r(rng);
}

uint32_t pcg32_random_r(pcg32_random_t* rng)
{
    uint64_t oldstate = rng->state;
    // Advance internal state
    rng->state = oldstate * 6364136223846793005ULL + (rng->inc | 1);
    // Calculate output function (XSH-RR), uses old state for max instruction parallelism
    uint32_t xorshifted = (uint32_t)(((oldstate >> 18u) ^ oldstate) >> 27u);
    uint32_t rot = (uint32_t)(oldstate >> 59u);
    return (xorshifted >> rot) | (xorshifted << ((-rot) & 31));
}

// Helpers
void InitSimRNG(uint64_t seed)
{
    // Sim RNG uses a fixed sequence ID to ensure it is distinct from others if we add more
    pcg32_srandom_r(&gSimRNG, seed, 0x54u); 
}

void InitLocalRNG(void)
{
    // Local RNG seeded with time, sequence distinct from Sim
    pcg32_srandom_r(&gLocalRNG, (uint64_t)time(NULL), 0x99u);
}

uint32_t SimRandom(void)
{
    return pcg32_random_r(&gSimRNG);
}

uint32_t LocalRandom(void)
{
    return pcg32_random_r(&gLocalRNG);
}
