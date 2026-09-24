/*
 * mod-coa-playerbots
 *
 * Advances the bot's current WorldTask by one step per tick: quest objectives go to the
 * QuestExecutor (and its objective handlers), accepting and handing in quests to QuestInteraction,
 * hub travel is handled here. Also owns the travel helper every executor shares, so mounting,
 * flying and stuck handling behave the same whatever the trip is for.
 */

#ifndef COA_PLAYERBOTS_WORLD_EXECUTOR_H
#define COA_PLAYERBOTS_WORLD_EXECUTOR_H

#include "BotMovement.h"
#include "WorldBrainState.h"

class Player;

enum class ExecResult : uint8
{
    Running,
    Completed,
    Failed,
};

namespace WorldExecutor
{
    ExecResult Update(Player* bot, BrainState& state, uint32 diff);

    // Goal-directed travel for the current task: mounts for long trips (per-bot threshold, never
    // in combat or right after a dismount), reports the destination to the population heatmap,
    // then BotMovement::Navigate. `sub` picks the goal id (WorldGoalSub::*).
    NavStatus TravelTo(Player* bot, BrainState& state, uint8 sub, float x, float y, float z, float radius,
        uint64 goalSalt = 0, bool allowMount = true);

    // Asks the ambient layer to fly the bot toward (x, y, z) when that is worth it. True when a
    // flight was requested (the ambient layer takes over from the next tick).
    bool TryRequestFlight(Player* bot, BrainState& state, float x, float y, float z);

    void Dismount(Player* bot, BrainState& state);

    // Drops everything the current task holds: movement claim, target claim, area occupancy,
    // heatmap destination. Called whenever a task ends, for whatever reason.
    void ReleaseTask(Player* bot, BrainState& state);
}

#endif // COA_PLAYERBOTS_WORLD_EXECUTOR_H
