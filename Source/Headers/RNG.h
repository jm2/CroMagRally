#pragma once

#include <stdint.h>

// PCG-Basic State
typedef struct {
    uint64_t state;
    uint64_t inc;
} pcg32_random_t;

// Extern the two separate RNG instances
extern pcg32_random_t gSimRNG;   // For gameplay logic (Physics, AI, Level Gen) - SYNCED
extern pcg32_random_t gLocalRNG; // For visual effects (Particles, Debris) - LOCAL/UNSYNCED

// Core PCG Functions
void pcg32_srandom_r(pcg32_random_t* rng, uint64_t initstate, uint64_t initseq);
uint32_t pcg32_random_r(pcg32_random_t* rng);

// Helper Wrappers for global instances
void InitSimRNG(uint64_t seed);
void InitLocalRNG(void);

uint32_t SimRandom(void);
uint32_t LocalRandom(void);
