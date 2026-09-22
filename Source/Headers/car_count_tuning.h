//
// car_count_tuning.h
//
// Scale factors for gameplay tuning that Pangea balanced for a field of six cars.
// They only depend on the car count, which every network peer shares, and are
// exactly 1 for six cars or fewer, so those races play exactly as before.
//

#pragma once

#define	TUNED_NUM_CARS		6				// field size the per-place and POW tuning was made for

// Per-place catch-up bonuses grow with how far back a car is. Above six cars the
// per-place step shrinks by 5/(numCars-1), so last place still gets what 6th place
// got in a six-car race.
float GetCatchUpPlaceScale(int numCars);
