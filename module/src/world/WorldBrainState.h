/*
 * mod-coa-playerbots
 *
 * The per-bot state of the open-world layer, shared by WorldBrain, WorldPlanner, WorldExecutor
 * and the objective handlers (all under src/world/). Nothing outside that directory includes
 * this: BotAI talks to the layer only through WorldBrain.h.
 */

#ifndef COA_PLAYERBOTS_WORLD_BRAIN_STATE_H
#define COA_PLAYERBOTS_WORLD_BRAIN_STATE_H

#include "FailureMemory.h"
#include "Log.h"
#include "ObjectGuid.h"
#include "StringFormat.h"
#include "WorldBrainConfig.h"
#include "WorldMetrics.h"
#include "WorldTask.h"
#include <string>
#include <unordered_map>
#include <vector>

class Player;

// One step of the route the planner has in mind, kept for `.botcmd brain`. Only the first step is
// executed; the rest is re-planned once it is done, because the world moves on in the meantime.
struct RouteStep
{
    WorldTaskType type = WorldTaskType::None;
    uint32 questId = 0;
    uint8 objectiveIndex = 0;
    float x = 0.0f;
    float y = 0.0f;
    float utility = 0.0f;
};

struct BrainState
{
    WorldPersona persona;
    WorldGoal goal = WorldGoal::None;
    WorldTask task;
    uint32 nextTaskId = 1;

    uint32 nextPlanMs = 0;
    uint32 lastUpdateMs = 0;          // last brain tick: a longer gap means the bot was busy elsewhere
    uint32 lastMapId = 0xFFFFFFFF;
    float lastX = 0.0f;               // position at the last brain tick (teleport detection)
    float lastY = 0.0f;
    bool suspended = false;
    SuspendReason suspendReason = SuspendReason::None;

    FailureMemory failures;
    // How many times each quest has been suspended; past a limit it is abandoned outright.
    std::unordered_map<uint32, uint32> questSuspensions;

    WorldMetrics metrics;
    std::vector<RouteStep> route;

    // The fallback activity (gather/fish/grind/ambient) chosen when there is no task, kept for a
    // while so the bot does not flip between activities every planner cycle.
    WorldDirective activity = WorldDirective::Idle;
    uint32 activityUntilMs = 0;
    uint32 activityIdleSinceMs = 0;

    // Opportunity interrupt (a herb next to the road): the primary task pauses while it runs.
    bool opportunityActive = false;
    WorldDirective opportunity = WorldDirective::Idle;
    uint32 opportunityUntilMs = 0;
    uint32 nextOpportunityCheckMs = 0;

    // Social layer (WorldSocial, WorldParties).
    uint32 nextSocialCheckMs = 0;
    bool socialActive = false;        // a help action (a resurrection cast) owns the bot
    uint32 socialUntilMs = 0;
    uint32 rezSpellId = 0;            // best resurrection spell the bot knows, 0 = none
    uint32 rezSpellCheckedMs = 0;
    uint32 nextPartyAttemptMs = 0;

    // Session rhythm: quest for a while, then take a break in town.
    uint32 sessionStartMs = 0;
    uint32 sessionLengthMs = 0;
    uint32 breakUntilMs = 0;

    // Planner passes in a row that found nothing: the next pass waits exponentially longer, so a
    // bot with nothing to do does not re-evaluate the whole neighbourhood every few seconds.
    uint32 emptyPlans = 0;
    // The last planner pass found no quest work and no hub on this map at all.
    bool noWorkOnMap = false;
    uint32 nextRelocationAskMs = 0;
    uint32 nextLogCleanupMs = 0;

    uint32 nextPresenceMs = 0;
    uint32 lastDismountMs = 0;
    float mountThreshold = 0.0f;

    uint32 seed = 0;
    uint32 generation = 0;

    std::string lastEvent;
    uint32 lastEventMs = 0;
};

namespace WorldBrainInternal
{
    uint32 NowMs();

    // Deterministic per-bot randomness: stable for a bot, different between bots, different
    // between successive decisions. No shared RNG state, so thousands of bots never synchronise.
    uint32 Roll(BrainState& state, uint32 salt);
    uint32 RollRange(BrainState& state, uint32 salt, uint32 lo, uint32 hi);
    float RollFloat(BrainState& state, uint32 salt);

    // Phase transition with a debug log line and a fresh phase clock.
    void SetPhase(Player* bot, BrainState& state, TaskPhase phase, char const* why);

    // Records a notable event for `.botcmd brain`'s "last event" line.
    void NoteEvent(BrainState& state, std::string text);

    // Ms spent in the current phase.
    uint32 PhaseElapsed(BrainState const& state);

    // Another bot's brain, for the social layer (nullptr when it has none yet).
    BrainState* FindState(ObjectGuid guid);
}

#endif // COA_PLAYERBOTS_WORLD_BRAIN_STATE_H
