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
    // in combat or right after a dismount), then BotMovement::Navigate. `sub` picks the goal id
    // (WorldGoalSub::*). The heatmap is not touched here: whether the bot counts as incoming is a
    // property of the task's phase (SyncIncoming), not of every little walk toward a mob.
    NavStatus TravelTo(Player* bot, BrainState& state, uint8 sub, float x, float y, float z, float radius,
        uint64 goalSalt = 0, bool allowMount = true);

    // Asks the ambient layer to fly the bot toward (x, y, z) when that is worth it. True when a
    // flight was requested (the ambient layer takes over from the next tick).
    bool TryRequestFlight(Player* bot, BrainState& state, float x, float y, float z);

    void Dismount(Player* bot, BrainState& state);

    // Something outside the task takes the bot for a while (an ambient errand, a suspension): the
    // task's clocks stop (WorldTask::Pause), its movement and live-target claim are let go, and it
    // stops counting as incoming. The task itself -- quest, objective, area -- is kept.
    void PauseTask(Player* bot, BrainState& state, char const* why);

    // Ends a pause: every clock of the task moves on by the pause's length, so the interruption
    // never counts toward a phase budget or the task deadline.
    void ResumeTask(Player* bot, BrainState& state);

    // The time budget of the task's current phase, as its executor enforces it (0: none).
    uint32 PhaseBudgetMs(BrainState const& state);

    // Makes the heatmap's incoming entry for the bot match WorldTask::CountsAsIncoming. Called
    // after every executor step; cheap (a no-op when nothing changed).
    void SyncIncoming(Player* bot, BrainState const& state);

    // Drops everything the current task holds: movement claim and request, target claim, area
    // occupancy, heatmap destination. Called whenever a task ends, for whatever reason, and when
    // the brain is suspended.
    void ReleaseTask(Player* bot, BrainState& state);
}

#endif // COA_PLAYERBOTS_WORLD_EXECUTOR_H
