/*
 * mod-coa-playerbots
 *
 * The building blocks every objective handler composes: get to the area, find something live to
 * work on and claim it, walk up to it, give up on it properly. Shared so that "what counts as
 * stuck", "how long to search", "how a claim is released" are decided once for every objective
 * kind instead of drifting apart per handler.
 */

#ifndef COA_PLAYERBOTS_OBJECTIVE_COMMON_H
#define COA_PLAYERBOTS_OBJECTIVE_COMMON_H

#include "BotMovement.h"
#include "IQuestObjectiveHandler.h"
#include <functional>
#include <unordered_map>
#include <unordered_set>

class Creature;
class GameObject;
class WorldObject;

namespace ObjectiveCommon
{
    // Current counter of one objective, straight from the bot's quest log / bags.
    uint32 CurrentCount(Player* bot, uint32 questId, ObjectiveDef const& def);
    bool IsDone(Player* bot, uint32 questId, ObjectiveDef const& def);

    // Summed progress of the task's objective and its bundle: "did this attempt advance anything".
    uint32 TaskProgress(Player* bot, WorldTask const& task);

    // Entries worth looking for right now: the objective's own targets plus those of every bundled
    // objective of the same kind (creature or object) that is not done yet. `served` counts how
    // many objectives each entry advances, which is what makes a double-duty mob score higher.
    void WantedTargets(ObjectiveContext const& ctx, bool gameObjects, std::unordered_set<uint32>& wanted,
        std::unordered_map<uint32, uint32>& served);

    bool InArea(ObjectiveContext const& ctx, float slack = 15.0f);

    // TravelToArea phase: walks (mounting when far) to the task's area. Moves the task to
    // Search on arrival, escalates to FailArea when the trip is stuck or out of time.
    ObjectiveResult TravelToArea(ObjectiveContext& ctx);

    // Gives up on the current area (remembered for a while) and moves the task to the next best
    // one; Failed when there is none left or the objective has used up its area budget.
    ObjectiveResult FailArea(ObjectiveContext& ctx, FailureReason reason);

    // Scores the live creatures of `wanted` around the bot and claims the best free one.
    // `acceptHostileOnly` for kill/collect; cast objectives may target friendly units.
    // `corpses` returns how many dead wanted creatures lie around (respawn is coming).
    Creature* FindCreature(ObjectiveContext& ctx, std::unordered_set<uint32> const& wanted,
        std::unordered_map<uint32, uint32> const& served, bool acceptHostileOnly, bool wantDead, uint32& corpses);

    // Same for objects; `usable` adds the handler's own readiness test.
    GameObject* FindObject(ObjectiveContext& ctx, std::unordered_set<uint32> const& wanted,
        std::function<bool(GameObject*)> const& usable);

    // Walks toward a live target until within `range` (and, for creatures, in line of sight).
    NavStatus Approach(ObjectiveContext& ctx, WorldObject* target, float range);

    // Nothing to do here right now: stroll between the area's spawn points, looking around.
    void Wander(ObjectiveContext& ctx);

    // Drops the claim on the current target (explicit fast path; claims also expire).
    void ReleaseTarget(ObjectiveContext& ctx);

    // The current target failed (unreachable, evading, no credit): remember it and let it go.
    void ForgetTarget(ObjectiveContext& ctx, FailureReason reason);

    // True once the bot is standing still and able to interact; stops it if it isn't.
    bool Settle(Player* bot, BrainState& state);

    // Enters the Search phase with a short human pause first.
    void BeginSearch(ObjectiveContext& ctx, char const* why);

    // Finished an attempt: records whether it advanced anything and, after too many dry
    // attempts, fails the objective (NoProgress). Returns Running/Completed/Failed.
    ObjectiveResult VerifyAttempt(ObjectiveContext& ctx, uint32 dryLimit);

    // How many dry attempts are reasonable for this objective before calling it broken: a 15%
    // drop needs more kills than a 100% one.
    uint32 DryAttemptLimit(ObjectiveDef const& def);
}

#endif // COA_PLAYERBOTS_OBJECTIVE_COMMON_H
