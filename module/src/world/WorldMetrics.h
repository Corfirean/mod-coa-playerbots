/*
 * mod-coa-playerbots
 *
 * Debug counters for the open-world layer, kept per bot and summed globally, printed by
 * `.botcmd brain` and `.botcmd worldstats`. They answer the questions a live test actually asks:
 * are quests being finished, how often do bots get stuck, how often do two bots want the same mob.
 */

#ifndef COA_PLAYERBOTS_WORLD_METRICS_H
#define COA_PLAYERBOTS_WORLD_METRICS_H

#include "Define.h"

struct WorldMetrics
{
    uint64 questsAccepted = 0;
    uint64 questsCompleted = 0;       // objectives all done (quest log shows complete)
    uint64 questsTurnedIn = 0;
    uint64 questsAbandoned = 0;
    uint64 questsSuspended = 0;
    uint64 objectivesCompleted = 0;
    uint64 objectiveProgress = 0;     // individual counter increments
    uint64 killTargetsSelected = 0;
    uint64 lootObjectivesCompleted = 0;
    uint64 useObjectivesCompleted = 0;
    uint64 exploreObjectivesCompleted = 0;
    uint64 tasksStarted = 0;
    uint64 tasksCompleted = 0;
    uint64 tasksFailed = 0;
    uint64 failedObjectives = 0;
    uint64 replans = 0;
    uint64 travels = 0;
    uint64 travelRetries = 0;
    uint64 movementStuck = 0;
    uint64 reservationConflicts = 0;
    uint64 areaSwitches = 0;
    uint64 opportunities = 0;
    uint64 breaks = 0;
};

namespace WorldMetricsGlobal
{
    WorldMetrics& Get();
}

// Bumps a counter on the bot's own metrics and the global ones in one go:
//   Count(state.metrics, &WorldMetrics::questsTurnedIn);
inline void Count(WorldMetrics& bot, uint64 WorldMetrics::* field, uint64 amount = 1)
{
    bot.*field += amount;
    WorldMetricsGlobal::Get().*field += amount;
}

#endif // COA_PLAYERBOTS_WORLD_METRICS_H
