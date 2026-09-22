/*
 * mod-coa-playerbots
 *
 * Data-Driven Combat AI Framework: TargetEvaluator implementation
 */

#include "engine/TargetEvaluator.h"
#include "engine/SpellPredicates.h"
#include "BotAI.h"
#include "Group.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "Timer.h"
#include <limits>
#include <unordered_map>
#include <vector>

namespace BotAI
{
    namespace
    {
        // A candidate must outscore the current target by this much to replace it -- keeps the
        // bot from re-targeting every tick between two similarly-threatening enemies.
        constexpr float STICKINESS_MARGIN = 1.25f;
        constexpr float UNREACHABLE_SCORE = -1000.0f;

        // Minimum time a voluntary target switch has to "stick" before another voluntary switch
        // is allowed -- confirmed live (Phase 2 testing) that the score margin alone still let
        // two similarly-dangerous enemies (e.g. one of them mid-cast this exact tick, the other
        // the next) flip the winner every recompute, since the margin check has no memory of the
        // last switch. Does not apply to a forced switch (current target gone bad -- dead,
        // unreachable, now CC'd), only to "found something merely better."
        constexpr uint32 MIN_TARGET_LOCK_MS = 3000;

        // Separate from the lock (item 13, Phase 2 fixup): once the lock itself has expired, this
        // paces how often a full nearby-hostile scan + rescoring happens at all, so a bot sitting
        // on a long fight doesn't re-scan every single ~100ms world tick just because the lock
        // is technically off. The lock is about stability (don't abandon a target too readily);
        // this is purely about not repeating an expensive scan needlessly. A forced event
        // (current target gone bad) always bypasses this, same as it bypasses the lock.
        constexpr uint32 EVALUATION_INTERVAL_MS = 300;

        struct TargetLock
        {
            ObjectGuid targetGuid;
            uint32 lockedAtMs = 0;
        };
        std::unordered_map<ObjectGuid, TargetLock> s_targetLocks;
        std::unordered_map<ObjectGuid, uint32> s_nextEvaluationAt;
    }

    bool TargetEvaluator::IsEngagedCandidate(Player* bot, Unit* candidate)
    {
        if (!bot || !candidate)
            return false;

        if (bot->GetVictim() == candidate || candidate->GetVictim() == bot)
            return true;
        if (bot->IsInCombatWith(candidate))
            return true;

        if (Group* group = bot->GetGroup())
        {
            for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
            {
                Player* member = ref->GetSource();
                if (!member || !member->IsAlive() || !member->IsInWorld())
                    continue;
                if (member->GetVictim() == candidate || candidate->GetVictim() == member)
                    return true;
                if (member->IsInCombatWith(candidate))
                    return true;
            }
        }

        return false;
    }

    float TargetEvaluator::ScoreTarget(Player* bot, Unit* candidate)
    {
        if (!bot || !candidate || !candidate->IsAlive())
            return UNREACHABLE_SCORE;
        if (!bot->IsValidAttackTarget(candidate) || !bot->IsWithinLOSInMap(candidate))
            return UNREACHABLE_SCORE;

        // Don't drag the group into breaking a groupmate's crowd control just to pile onto a
        // "better" target -- see item 1/#8 and item 17.
        if (IsUnitUnderBreakableCrowdControl(candidate))
            return UNREACHABLE_SCORE * 0.5f; // still ranked below any legitimate candidate

        float score = 100.0f;
        Group* group = bot->GetGroup();

        if (group)
        {
            // Marked/leader target: the group leader already fighting exactly this unit is the
            // closest cheap proxy this codebase has to a real raid-icon marked target.
            if (Player* leader = ObjectAccessor::FindPlayer(group->GetLeaderGUID()))
            {
                if (leader != bot && (leader->GetVictim() == candidate || leader->GetSelectedUnit() == candidate))
                    score += 300.0f;
            }

            // Tank's current target: DPS/healers should converge on what the tank already holds.
            for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
            {
                Player* member = ref->GetSource();
                if (member && member != bot && member->GetVictim() == candidate &&
                    BotAI::GetRole(member->GetGUID()) == BotRole::Tank)
                {
                    score += 150.0f;
                    break;
                }
            }
        }

        // Enemy attacking the healer -- a loose add on the healer outranks a "better" target
        // that's simply sitting there not threatening anyone.
        if (Unit* victim = candidate->GetVictim())
        {
            if (Player* victimPlayer = victim->ToPlayer())
                if (BotAI::GetRole(victimPlayer->GetGUID()) == BotRole::Healer)
                    score += 200.0f;
        }

        // Dangerous caster -- mid-cast right now.
        if (IsTargetCastingInterruptibleSpell(candidate))
            score += 120.0f;

        // Below execute HP -- finish it rather than open a second fight.
        float hpPct = candidate->GetHealthPct();
        if (hpPct < 20.0f)
            score += (20.0f - hpPct) * 4.0f;

        // Distance penalty -- a closer, merely-ordinary target usually beats a marginally better
        // one across the room.
        score -= bot->GetDistance(candidate) * 2.0f;

        return score;
    }

    Unit* TargetEvaluator::RefineTarget(Player* bot, Unit* currentTarget, float scanRange)
    {
        if (!bot)
            return nullptr;

        ObjectGuid botGuid = bot->GetGUID();
        uint32 now = getMSTime();

        float currentScore = std::numeric_limits<float>::lowest();
        bool currentValid = currentTarget && currentTarget->IsAlive() && bot->IsValidAttackTarget(currentTarget);
        if (currentValid)
            currentScore = ScoreTarget(bot, currentTarget);
        bool currentIsBad = !currentValid || currentScore <= UNREACHABLE_SCORE * 0.5f;

        if (!currentIsBad)
        {
            // An active lock on the current target blocks another *voluntary* switch (see
            // MIN_TARGET_LOCK_MS) -- a forced one (current target went bad) always bypasses it.
            auto lockItr = s_targetLocks.find(botGuid);
            if (lockItr != s_targetLocks.end() && lockItr->second.targetGuid == currentTarget->GetGUID() &&
                now - lockItr->second.lockedAtMs < MIN_TARGET_LOCK_MS)
            {
                return currentTarget;
            }

            // Lock's expired, but don't re-scan every single tick just because of that (item 13)
            // -- a forced event always bypasses this too, same as the lock above.
            auto evalItr = s_nextEvaluationAt.find(botGuid);
            if (evalItr != s_nextEvaluationAt.end() && now < evalItr->second)
                return currentTarget;
        }
        s_nextEvaluationAt[botGuid] = now + EVALUATION_INTERVAL_MS;

        std::vector<Unit*> candidates;
        GetNearbyEnemies(bot, bot, scanRange, candidates);

        Unit* best = nullptr;
        float bestScore = std::numeric_limits<float>::lowest();
        for (Unit* candidate : candidates)
        {
            if (candidate == currentTarget)
                continue;
            // Engaged-only (item 5, Phase 2 fixup): never pull in a nearby mob that isn't
            // already part of this fight -- see IsEngagedCandidate's own comment on why that's
            // a different system's job entirely.
            if (!IsEngagedCandidate(bot, candidate))
                continue;
            float score = ScoreTarget(bot, candidate);
            if (score > bestScore)
            {
                bestScore = score;
                best = candidate;
            }
        }

        auto lockOnto = [&](Unit* target) -> Unit*
        {
            if (target)
                s_targetLocks[botGuid] = TargetLock{ target->GetGUID(), now };
            return target;
        };

        if (currentValid)
        {
            // Force a switch away from a target that's gone bad (unreachable, now CC'd) even
            // without a positive alternative in range; otherwise require a clear improvement.
            if (currentIsBad)
                return lockOnto((best && bestScore > 0.0f) ? best : currentTarget);
            if (best && bestScore > currentScore * STICKINESS_MARGIN)
                return lockOnto(best);

            // Staying put -- make sure a lock exists for this target (e.g. it was assigned
            // externally and this is the first time RefineTarget has seen it) without resetting
            // an already-running timer just because this tick re-confirmed the same target.
            auto lockItr = s_targetLocks.find(botGuid);
            if (lockItr == s_targetLocks.end() || lockItr->second.targetGuid != currentTarget->GetGUID())
                s_targetLocks[botGuid] = TargetLock{ currentTarget->GetGUID(), now };
            return currentTarget;
        }

        return lockOnto((best && bestScore > 0.0f) ? best : nullptr);
    }

    Unit* TargetEvaluator::FindEngagedAlternative(Player* bot, Unit* exclude, float scanRange)
    {
        if (!bot)
            return nullptr;

        std::vector<Unit*> candidates;
        GetNearbyEnemies(bot, bot, scanRange, candidates);

        Unit* best = nullptr;
        float bestScore = 0.0f;
        for (Unit* candidate : candidates)
        {
            if (candidate == exclude)
                continue;
            if (!IsEngagedCandidate(bot, candidate))
                continue;
            // Don't hop from one CC'd target straight onto another one.
            if (IsUnitUnderBreakableCrowdControl(candidate))
                continue;

            float score = ScoreTarget(bot, candidate);
            if (score > bestScore)
            {
                bestScore = score;
                best = candidate;
            }
        }
        return best;
    }

    Unit* TargetEvaluator::FindBestAoECluster(Player* bot, Unit* fallbackTarget, float clusterRadius, float scanRange)
    {
        if (!bot)
            return fallbackTarget;

        std::vector<Unit*> candidates;
        GetNearbyEnemies(bot, bot, scanRange, candidates);

        std::vector<Unit*> engaged;
        if (fallbackTarget)
            engaged.push_back(fallbackTarget);
        for (Unit* candidate : candidates)
        {
            if (candidate == fallbackTarget)
                continue;
            if (IsEngagedCandidate(bot, candidate))
                engaged.push_back(candidate);
        }

        if (engaged.size() <= 1)
            return fallbackTarget;

        Unit* best = fallbackTarget;
        size_t bestClusterSize = 0;
        for (Unit* center : engaged)
        {
            size_t clusterSize = 0;
            for (Unit* other : engaged)
            {
                if (other != center && center->GetDistance(other) <= clusterRadius)
                    ++clusterSize;
            }
            if (clusterSize > bestClusterSize)
            {
                bestClusterSize = clusterSize;
                best = center;
            }
        }
        return best;
    }

    void TargetEvaluator::ForgetBot(ObjectGuid botGuid)
    {
        s_targetLocks.erase(botGuid);
        s_nextEvaluationAt.erase(botGuid);
    }
}
