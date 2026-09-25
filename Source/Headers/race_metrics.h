//
// race_metrics.h
//
// Smoke-only race metrics (--smoke-metrics). Off by default: every call from
// gameplay code is guarded by gRaceMetricsEnabled, so a normal game pays one
// untaken branch per hook.
//
// At the end of a --smoke-test-frames practice race, ReportRaceMetrics logs one
// race line and one line per car. Keys and value formats are stable, so scripts
// can parse them as key=value pairs.
//
//   METRICS race track=T cars=N why=W racetime=S ckpts=C
//     track     1-based track number
//     cars      cars in the race (gNumTotalPlayers)
//     why       player1-finished (--smoke-until-finish), race-complete (after the
//               finish cooldown) or frame-cap (--smoke-test-frames reached first)
//     racetime  seconds of simulated racing (after the starting light)
//     ckpts     checkpoints per lap
//
//   METRICS car p=P cpu=0|1 veh=V place=K lap=L ckpt=C done=0|1 fin=S finorder=K
//               lap0=S ck5=S lap1=S lap2=S stuck=S wrong=S
//               bumps=N hard=N blasted=N pickups=N uses=N nbr=X
//     p         0-based player number; player 0 is the human (AI-driven with --smoke-autopilot)
//     cpu       1 for a CPU car
//     veh       vehicle type
//     place, lap, ckpt, done
//               0-based race place, lap, checkpoint and race-complete flag at the end
//     fin       racetime when the car finished, or -1
//     finorder  0-based finishing order, or -1
//     lap0, lap1, lap2
//               racetime when the car first counted lap 0, 1 and 2, or -1
//     ck5       racetime when the car first reached checkpoint 5 on lap 0, or -1
//     stuck     seconds below RACE_METRICS_STUCK_SPEED before finishing
//     wrong     seconds driving the wrong way before finishing
//     bumps     car-to-car collisions
//     hard      car-to-car collisions above RACE_METRICS_HARD_HIT_SPEED relative speed
//     blasted   times caught in another car's blast
//     pickups   POW pickups
//     uses      POW activation attempts
//     nbr       mean number of other cars within RACE_METRICS_NEIGHBOR_RADIUS
//
// Times use %.1f, nbr uses %.2f, everything else is an integer.
//

#pragma once

#define	RACE_METRICS_STUCK_SPEED		400.0f		// Speed2D below this counts as stuck
#define	RACE_METRICS_HARD_HIT_SPEED		1200.0f		// relative speed of a hard car-to-car hit
#define	RACE_METRICS_NEIGHBOR_RADIUS	2500.0f		// "nearby" radius for nbr

extern Boolean	gRaceMetricsEnabled;

void ResetRaceMetrics(void);
void SampleRaceMetrics(const PlayerInfoType *players, int numPlayers, long numCheckpoints, float dt);
void ReportRaceMetrics(const char *why, int trackNum, const PlayerInfoType *players, int numPlayers, long numCheckpoints);

		/* EVENT HOOKS (CALL ONLY WHEN gRaceMetricsEnabled) */

void RaceMetricsCarHit(short playerA, short playerB, float relSpeed);
void RaceMetricsBlast(short victim, short thrower);
void RaceMetricsPickup(short playerNum);
void RaceMetricsPOWUse(short playerNum);

		/* LINE FORMATTING (FOR ReportRaceMetrics AND TESTS) */

void FormatRaceMetricsRace(char *buf, size_t size, const char *why, int trackNum, int numPlayers, long numCheckpoints);
void FormatRaceMetricsCar(char *buf, size_t size, int playerNum, const PlayerInfoType *player);
