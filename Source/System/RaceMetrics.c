/****************************/
/*   	RACE METRICS.C	    */
/****************************/

//
// Smoke-only race metrics for --smoke-metrics soak runs (see race_metrics.h for the
// output format). Nothing here runs unless gRaceMetricsEnabled is set.
//

#include "game.h"
#include "race_metrics.h"


/****************************/
/*    CONSTANTS             */
/****************************/

#define	NUM_LAP_MARKS			3				// lap0..lap2
#define	METRICS_CHECKPOINT		5				// ck5 marks lap 0 checkpoint 5
#define	MAX_METRICS_LINE		512

typedef struct
{
	int		bumps, hardHits, blasted, pickups, uses;
	float	stuckTime, wrongWayTime, neighborSum;
	float	lapAt[NUM_LAP_MARKS], checkpointAt, finishAt;
	int		finishOrder;
}CarMetrics;


/*********************/
/*    VARIABLES      */
/*********************/

Boolean				gRaceMetricsEnabled = false;

static float		gRaceTime, gNeighborSamples;
static int			gFinishCount;
static CarMetrics	gCarMetrics[MAX_PLAYERS];


static Boolean IsMetricsPlayer(short playerNum)
{
	return playerNum >= 0 && playerNum < MAX_PLAYERS;
}


/******************** RESET RACE METRICS ************************/

void ResetRaceMetrics(void)
{
	gRaceTime = 0;
	gNeighborSamples = 0;
	gFinishCount = 0;

	for (int p = 0; p < MAX_PLAYERS; p++)
	{
		CarMetrics	*m = &gCarMetrics[p];

		SDL_memset(m, 0, sizeof(*m));
		for (int k = 0; k < NUM_LAP_MARKS; k++)
			m->lapAt[k] = -1;
		m->checkpointAt = -1;
		m->finishAt = -1;
		m->finishOrder = -1;
	}
}


/******************** SAMPLE RACE METRICS ************************/
//
// Call once per simulation step while cars have control.
//

void SampleRaceMetrics(const PlayerInfoType *players, int numPlayers, long numCheckpoints, float dt)
{
	if (numPlayers > MAX_PLAYERS)
		numPlayers = MAX_PLAYERS;

	gRaceTime += dt;
	gNeighborSamples += 1;

	for (int p = 0; p < numPlayers; p++)
	{
		const PlayerInfoType	*pi = &players[p];
		CarMetrics				*m = &gCarMetrics[p];

		if (!pi->objNode)
			continue;

		if (!pi->raceComplete)
		{
			if (pi->objNode->Speed2D < RACE_METRICS_STUCK_SPEED)
				m->stuckTime += dt;
			if (pi->wrongWay)
				m->wrongWayTime += dt;
		}

		int lap = pi->lapNum;
		if (lap >= 0 && lap < NUM_LAP_MARKS && m->lapAt[lap] < 0)
			m->lapAt[lap] = gRaceTime;

				/* CARS THAT START BEHIND THE LAST CHECKPOINT PASS IT BEFORE LAP 0 */

		if (lap == 0 && pi->checkpointNum >= METRICS_CHECKPOINT && pi->checkpointNum < numCheckpoints - 1
			&& m->checkpointAt < 0)
		{
			m->checkpointAt = gRaceTime;
		}

		if (pi->raceComplete && m->finishAt < 0)
		{
			m->finishAt = gRaceTime;
			m->finishOrder = gFinishCount++;
		}

				/* COUNT NEARBY CARS */

		int n = 0;
		for (int q = 0; q < numPlayers; q++)
		{
			if (q == p)
				continue;
			float dx = players[q].coord.x - pi->coord.x;
			float dz = players[q].coord.z - pi->coord.z;
			if (dx*dx + dz*dz < RACE_METRICS_NEIGHBOR_RADIUS * RACE_METRICS_NEIGHBOR_RADIUS)
				n++;
		}
		m->neighborSum += n;
	}
}


/******************** EVENT HOOKS ************************/

void RaceMetricsCarHit(short playerA, short playerB, float relSpeed)
{
	Boolean	hard = relSpeed > RACE_METRICS_HARD_HIT_SPEED;

	if (IsMetricsPlayer(playerA))
	{
		gCarMetrics[playerA].bumps++;
		gCarMetrics[playerA].hardHits += hard;
	}
	if (IsMetricsPlayer(playerB))
	{
		gCarMetrics[playerB].bumps++;
		gCarMetrics[playerB].hardHits += hard;
	}
}

void RaceMetricsBlast(short victim, short thrower)
{
	if (victim != thrower && IsMetricsPlayer(victim))
		gCarMetrics[victim].blasted++;
}

void RaceMetricsPickup(short playerNum)
{
	if (IsMetricsPlayer(playerNum))
		gCarMetrics[playerNum].pickups++;
}

void RaceMetricsPOWUse(short playerNum)
{
	if (IsMetricsPlayer(playerNum))
		gCarMetrics[playerNum].uses++;
}


/******************** FORMAT METRICS LINES ************************/

void FormatRaceMetricsRace(char *buf, size_t size, const char *why, int trackNum, int numPlayers, long numCheckpoints)
{
	SDL_snprintf(buf, size, "METRICS race track=%d cars=%d why=%s racetime=%.1f ckpts=%ld",
				trackNum, numPlayers, why, gRaceTime, numCheckpoints);
}

void FormatRaceMetricsCar(char *buf, size_t size, int playerNum, const PlayerInfoType *player)
{
	const CarMetrics	*m = &gCarMetrics[playerNum];

	SDL_snprintf(buf, size,
				"METRICS car p=%d cpu=%d veh=%d place=%d lap=%d ckpt=%d done=%d fin=%.1f finorder=%d"
				" lap0=%.1f ck5=%.1f lap1=%.1f lap2=%.1f stuck=%.1f wrong=%.1f"
				" bumps=%d hard=%d blasted=%d pickups=%d uses=%d nbr=%.2f",
				playerNum, player->isComputer ? 1 : 0, player->vehicleType, player->place, player->lapNum,
				player->checkpointNum, player->raceComplete ? 1 : 0, m->finishAt, m->finishOrder,
				m->lapAt[0], m->checkpointAt, m->lapAt[1], m->lapAt[2], m->stuckTime, m->wrongWayTime,
				m->bumps, m->hardHits, m->blasted, m->pickups, m->uses,
				gNeighborSamples > 0 ? m->neighborSum / gNeighborSamples : 0.0f);
}


/******************** REPORT RACE METRICS ************************/

void ReportRaceMetrics(const char *why, int trackNum, const PlayerInfoType *players, int numPlayers, long numCheckpoints)
{
char	line[MAX_METRICS_LINE];

	if (numPlayers > MAX_PLAYERS)
		numPlayers = MAX_PLAYERS;

	FormatRaceMetricsRace(line, sizeof(line), why, trackNum, numPlayers, numCheckpoints);
	SDL_Log("%s", line);

	for (int p = 0; p < numPlayers; p++)
	{
		FormatRaceMetricsCar(line, sizeof(line), p, &players[p]);
		SDL_Log("%s", line);
	}
}
