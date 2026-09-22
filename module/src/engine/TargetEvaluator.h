/*
 * mod-coa-playerbots
 *
 * Data-Driven Combat AI Framework: TargetEvaluator (item 1/#8, Phase 2)
 *
 * A group is too dependent on current target / leader target / whichever enemy happened to be
 * found first -- this scores nearby hostiles by how much they actually matter right now (leader/
 * marked target, tank's target, attacking the healer, mid-cast, low HP) and refines the bot's
 * already-acquired baseline target (see BotAI.cpp's UpdateOffensive) toward the best one, with a
 * stickiness margin so it doesn't ping-pong between similarly-scored enemies every tick.
 */

#ifndef COA_PLAYERBOTS_TARGET_EVALUATOR_H
#define COA_PLAYERBOTS_TARGET_EVALUATOR_H

#include "Define.h"
#include "ObjectGuid.h"

class Player;
class Unit;

namespace BotAI
{
    class TargetEvaluator
    {
    public:
        // Higher is better. A large negative score means "don't pick this" (unreachable, out of
        // LOS, or would break a groupmate's crowd control).
        static float ScoreTarget(Player* bot, Unit* candidate);

        // Re-evaluates currentTarget against nearby *engaged* hostiles (see IsEngagedCandidate)
        // within scanRange and returns the better choice, applying a stickiness margin (a
        // candidate must clearly outscore the current target to replace it, not just barely) AND
        // a minimum target-lock duration (see MIN_TARGET_LOCK_MS in the .cpp) -- confirmed live
        // during Phase 2 testing that the score margin alone wasn't enough: two similarly-
        // dangerous enemies trading which one is mid-cast this exact tick could still flip the
        // winner every tick, oscillating a bot between them indefinitely (score margin resets
        // each recompute, it has no memory). Also paced by a separate, shorter evaluation
        // interval once the lock itself has expired (item 13) -- distinct from the lock: the lock
        // is about *stability* (don't abandon a target too readily), the interval is about
        // *performance* (don't re-scan nearby hostiles every single tick just because the lock
        // happens to be off). A target that's gone bad (dead/unreachable/now CC'd) always bypasses
        // both. Returns currentTarget unchanged when it's still the best (or only) valid/locked
        // option, and nullptr only when neither currentTarget nor any scanned candidate qualifies
        // at all -- callers should treat that as "nothing to suggest," not "clear the target."
        static Unit* RefineTarget(Player* bot, Unit* currentTarget, float scanRange = 30.0f);

        // True when `candidate` is already part of this fight -- the current target, in a real
        // combat reference with the bot, or fighting (or being fought by) a groupmate (item 5,
        // Phase 2 fixup). TargetEvaluator answers "which already-engaged enemy is worth focusing
        // more," never "should I pull that mob standing over there" -- new pulls are the job of
        // dedicated systems (tank pull logic, Auto Dungeon, solo grind, manual Pull, BG
        // objectives), not this generic refinement pass.
        static bool IsEngagedCandidate(Player* bot, Unit* candidate);

        // Best *other* already-engaged, not-CC'd target within scanRange, or nullptr if there
        // isn't one -- used by the CC-protection policy (item 6) to find something else to hit
        // when the current target turns out to be under breakable crowd control.
        static Unit* FindEngagedAlternative(Player* bot, Unit* exclude, float scanRange = 30.0f);

        // Best AoE ground-target cluster center among already-engaged enemies (item 18, Phase 2
        // fixup) -- previously TargetType::AreaHostile was just an alias for CurrentTarget, which
        // this replaces. Picks whichever engaged enemy (including fallbackTarget itself) has the
        // most OTHER engaged enemies within clusterRadius of it; returns fallbackTarget unchanged
        // when nothing scores better (solo fight, or nothing else engaged nearby). Deliberately
        // never considers a mob that isn't already part of this fight, same as IsEngagedCandidate.
        static Unit* FindBestAoECluster(Player* bot, Unit* fallbackTarget, float clusterRadius = 10.0f, float scanRange = 30.0f);

        // Count of already-engaged hostiles (see IsEngagedCandidate) within range of center --
        // review finding #1 on the Phase 2 fixup pass: CombatContext previously sized AoE
        // eligibility off SpellPredicates::CountNearbyEnemies, which counts every attackable unit
        // in range regardless of whether it's part of this fight, so one engaged mob plus two
        // idle bystanders standing nearby could still satisfy an AoE ability's minAoETargets floor
        // and pull the bystanders in. This is the engaged-only equivalent CombatContext now uses
        // instead for that one field.
        static uint32 CountEngaged(Player* bot, Unit* center, float range = 10.0f);

        // Drops this bot's target-lock and evaluation-interval timers -- called on despawn (see
        // BotAI::Forget) so the maps don't grow across repeated spawn/despawn cycles.
        static void ForgetBot(ObjectGuid botGuid);
    };
}

#endif // COA_PLAYERBOTS_TARGET_EVALUATOR_H
