/*
 * mod-coa-playerbots
 *
 * Data-Driven Combat AI Framework: ActionEvaluator implementation
 */

#include "engine/ActionEvaluator.h"
#include "engine/HealEvaluator.h"
#include "engine/SpellPredicates.h"
#include "engine/SpellResolver.h"
#include "engine/TargetEvaluator.h"
#include "BotClassRotations.h"
#include "Group.h"
#include "Player.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "Timer.h"
#include <algorithm>
#include <unordered_map>

namespace BotAI
{
    namespace
    {
        // [botGuid][rootSpellId] -> expiryMSTime
        std::unordered_map<ObjectGuid, std::unordered_map<uint32, uint32>> s_internalThrottles;

        // True when `bot` could actually reach `candidate` with `spellInfo` right now -- range,
        // line of sight, alive, in-world, same map. Self is always reachable. Deliberately not the
        // full CanCast pipeline (cooldown/power/GCD don't vary by which ally is chosen, so
        // filtering candidates on them here would be redundant with the CanCast call that already
        // follows target resolution) -- just the target-specific geometry that made review finding
        // #3 possible in the first place: the most urgent ally being out of THIS spell's range.
        bool IsHealTargetReachable(Player* bot, Unit* candidate, SpellInfo const* spellInfo)
        {
            if (!bot || !candidate || !spellInfo || !candidate->IsAlive() || !candidate->IsInWorld())
                return false;
            if (candidate->GetMap() != bot->GetMap())
                return false;
            if (candidate == bot)
                return true;

            float dist = bot->GetDistance(candidate);
            float maxRange = spellInfo->GetMaxRange(true, bot);
            if (maxRange > 0.0f && dist > maxRange)
                return false;
            return bot->IsWithinLOSInMap(candidate);
        }

        // Shared triage scan behind AnyInjuredAlly/LowestHealthAlly/TankAlly (review finding #3):
        // best HealEvaluator::ScoreHealUrgency among group members (and the bot itself) that both
        // satisfy the descriptor's own constraints (HP threshold, aura-missing) AND are actually
        // reachable by the specific spell about to be cast -- not just the single best-urgency
        // ally group-wide, which could easily be out of THIS ability's range while a slightly-
        // less-urgent one sits well within it. `spellInfo` may be null (spell not resolved yet);
        // reachability is then skipped, matching the old unfiltered behavior.
        Player* SelectBestReachableAlly(CombatContext const& ctx, AbilityDescriptor const& desc, SpellInfo const* spellInfo, bool requireTankRole)
        {
            Player* best = nullptr;
            float bestUrgency = -1.0f;
            auto consider = [&](Player* candidate)
            {
                if (!candidate || !candidate->IsAlive() || !candidate->IsInWorld())
                    return;
                if (requireTankRole && BotAI::GetRole(candidate->GetGUID()) != BotRole::Tank)
                    return;
                if (candidate->GetHealthPct() > desc.maxTargetHpPct)
                    return;
                if (desc.requireAuraMissingOnTarget && candidate->HasAura(desc.rootSpellId, ctx.bot->GetGUID()))
                    return;
                if (spellInfo && !IsHealTargetReachable(ctx.bot, candidate, spellInfo))
                    return;

                float urgency = HealEvaluator::ScoreHealUrgency(ctx.bot, candidate);
                if (urgency > bestUrgency)
                {
                    bestUrgency = urgency;
                    best = candidate;
                }
            };

            if (Group const* group = ctx.bot->GetGroup())
                for (GroupReference const* ref = group->GetFirstMember(); ref; ref = ref->next())
                    consider(ref->GetSource());
            consider(ctx.bot);

            return best;
        }
    }

    bool ActionEvaluator::IsThrottled(ObjectGuid botGuid, uint32 rootSpellId)
    {
        auto botItr = s_internalThrottles.find(botGuid);
        if (botItr == s_internalThrottles.end())
            return false;

        auto spellItr = botItr->second.find(rootSpellId);
        if (spellItr == botItr->second.end())
            return false;

        uint32 now = getMSTime();
        if (now < spellItr->second)
            return true;

        botItr->second.erase(spellItr);
        return false;
    }

    void ActionEvaluator::SetThrottle(ObjectGuid botGuid, uint32 rootSpellId, uint32 durationMs)
    {
        s_internalThrottles[botGuid][rootSpellId] = getMSTime() + durationMs;
    }

    void ActionEvaluator::ClearThrottles(ObjectGuid botGuid)
    {
        s_internalThrottles.erase(botGuid);
    }

    Unit* ActionEvaluator::ResolveTarget(CombatContext const& ctx, TargetType targetType, AbilityDescriptor const& desc, uint32 resolvedSpellId)
    {
        switch (targetType)
        {
            case TargetType::Self:
                return ctx.bot;

            case TargetType::CurrentTarget:
                return (ctx.victim && ctx.victim->IsAlive()) ? ctx.victim : nullptr;

            case TargetType::AreaHostile:
            {
                // Real cluster-center pick among already-engaged enemies (item 18, Phase 2
                // fixup) -- previously identical to CurrentTarget. See TargetEvaluator::
                // FindBestAoECluster's own comment.
                if (!ctx.victim || !ctx.victim->IsAlive())
                    return nullptr;
                return TargetEvaluator::FindBestAoECluster(ctx.bot, ctx.victim);
            }

            // Ally triage target types (review finding #3 on the Phase 2 fixup pass): all three
            // route through SelectBestReachableAlly so the ally picked is the best-urgency one
            // this specific spell can actually reach, not just the single best-urgency ally
            // group-wide (which CombatContext computes once, shared by every ability regardless
            // of its own range) -- a healer that "sees the wounded but doesn't heal" because its
            // most urgent target happened to be out of THIS spell's range now considers the next
            // best reachable one instead of silently failing CanCast and skipping the ability.
            case TargetType::LowestHealthAlly:
            {
                SpellInfo const* spellInfo = resolvedSpellId ? sSpellMgr->GetSpellInfo(resolvedSpellId) : nullptr;
                return SelectBestReachableAlly(ctx, desc, spellInfo, false);
            }

            case TargetType::TankAlly:
            {
                SpellInfo const* spellInfo = resolvedSpellId ? sSpellMgr->GetSpellInfo(resolvedSpellId) : nullptr;
                if (Player* tank = SelectBestReachableAlly(ctx, desc, spellInfo, true))
                    return tank;
                // No reachable tank (none in group, or the only one(s) are out of range) -- fall
                // back to the best reachable ally regardless of role, same spirit as the old
                // "ctx.tankAlly ? ctx.tankAlly : ctx.lowestAlly" fallback.
                return SelectBestReachableAlly(ctx, desc, spellInfo, false);
            }

            case TargetType::AnyInjuredAlly:
            {
                SpellInfo const* spellInfo = resolvedSpellId ? sSpellMgr->GetSpellInfo(resolvedSpellId) : nullptr;
                return SelectBestReachableAlly(ctx, desc, spellInfo, false);
            }

            case TargetType::PartyMissingBuff:
            {
                if (Group const* group = ctx.bot->GetGroup())
                {
                    for (GroupReference const* ref = group->GetFirstMember(); ref; ref = ref->next())
                    {
                        Player* member = ref->GetSource();
                        if (member && member->IsAlive() && member->IsInWorld() && !member->HasAura(desc.rootSpellId))
                            return member;
                    }
                }
                return !ctx.bot->HasAura(desc.rootSpellId) ? ctx.bot : nullptr;
            }

            default:
                return ctx.victim;
        }
    }

    bool ActionEvaluator::CanCast(CombatContext const& ctx, AbilityDescriptor const& desc, uint32 resolvedSpellId, Unit* target)
    {
        if (!ctx.bot || !resolvedSpellId || !target)
            return false;

        Player* bot = ctx.bot;

        // Internal throttle check
        if (desc.internalThrottleMs > 0 && IsThrottled(bot->GetGUID(), desc.rootSpellId))
            return false;

        // Failure backoff cooldown (e.g. from previous failed casts)
        if (BotAI::IsSpellInFailureCooldown(bot->GetGUID(), resolvedSpellId))
            return false;

        // Native spell cooldown
        if (bot->HasSpellCooldown(resolvedSpellId))
            return false;

        SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(resolvedSpellId);
        if (!spellInfo)
            return false;

        // Real per-spell GCD (see item 6 of the combat-engine rework) -- off-GCD abilities
        // (StartRecoveryTime == 0) always pass this, matching IsOffGlobalCooldown's own comment.
        if (!IsOffGlobalCooldown(bot, spellInfo))
            return false;

        // Cheap pre-cast validation (item 17, Phase 2 fixup) -- catches a chunk of predictable
        // SPELL_FAILED_* outcomes before ever reaching CastSpell, without duplicating the real
        // Spell::CheckCast pipeline: a stunned caster can't start any new cast, line-of-sight
        // blocks any explicitly-targeted spell, and a hard immunity on the target rules this
        // spell out outright. Reduces how often a "predictable failure" eats the 500ms retry gate.
        if (bot->HasUnitState(UNIT_STATE_STUNNED))
            return false;
        if (target != bot)
        {
            if (spellInfo->NeedsExplicitUnitTarget() && !bot->IsWithinLOSInMap(target))
                return false;
            if (target->IsImmunedToSpell(spellInfo, bot))
                return false;
        }

        // AoE blast-radius CC guard (review finding #1 on the Phase 2 fixup pass): the
        // minAoETargets eligibility floor in ScoreAbility only looks at how many engaged enemies
        // are nearby, never whether one of them is something a groupmate deliberately sheeped/
        // feared/sapped -- an AoE landing on the primary target can still catch an incidental
        // CC'd mob standing inside its own blast radius even though the primary target itself is
        // never CC'd here (BotAI's own CC-protection policy already guarantees that before this
        // engine ever runs).
        if (HasTag(desc.tags, AbilityTag::AoEDamage) && WouldAoEHitBreakableCrowdControl(bot, target, spellInfo))
            return false;

        // Equipment / weapon requirement
        if (!bot->HasItemFitToSpellRequirements(spellInfo))
            return false;

        // Aura requirements on caster
        if (desc.requiredAuraOnCaster && !bot->HasAura(desc.requiredAuraOnCaster))
            return false;
        if (desc.missingAuraOnCaster && bot->HasAura(desc.missingAuraOnCaster))
            return false;

        if (spellInfo->CasterAuraState && !bot->HasAuraState(AuraStateType(spellInfo->CasterAuraState)))
            return false;
        if (spellInfo->CasterAuraSpell && !bot->HasAura(spellInfo->CasterAuraSpell))
            return false;

        // Target aura requirements
        if (desc.requireAuraMissingOnTarget && target->HasAura(resolvedSpellId, bot->GetGUID()))
            return false;

        if (spellInfo->TargetAuraState && !target->HasAuraState(AuraStateType(spellInfo->TargetAuraState)))
            return false;
        if (spellInfo->TargetAuraSpell && !target->HasAura(spellInfo->TargetAuraSpell))
            return false;

        // Power cost check
        if (spellInfo->PowerType == POWER_HEALTH)
        {
            int32 cost = spellInfo->CalcPowerCost(bot, spellInfo->GetSchoolMask());
            if (cost > 0 && bot->GetHealth() <= static_cast<uint32>(cost))
                return false;
        }
        else
        {
            int32 cost = spellInfo->CalcPowerCost(bot, spellInfo->GetSchoolMask());
            if (cost > 0 && bot->GetPower(Powers(spellInfo->PowerType)) < cost)
                return false;
        }

        // Resource management hard floor (item 13, Phase 2): distinct from the raw affordability
        // check above -- this is a profile-authored "don't even consider this below X% power"
        // rule (e.g. a finisher that's not worth its real cost below some threshold), not "can
        // the bot literally pay for it." Defaults to 0 (unrestricted) for profiles that don't set it.
        if (desc.minPowerPct > 0.0f && ctx.botPowerPct < desc.minPowerPct)
            return false;

        // Range check
        bool positiveRange = spellInfo->IsPositive();
        if (target != bot)
        {
            float dist = bot->GetDistance(target);
            float maxRange = spellInfo->GetMaxRange(positiveRange, bot);
            if (maxRange > 0.0f && dist > maxRange)
                return false;

            float minRange = spellInfo->GetMinRange(positiveRange);
            if (minRange > 0.0f && bot->IsWithinRange(target, minRange + bot->GetMeleeRange(target)))
                return false;
        }

        return true;
    }

    float ActionEvaluator::ScoreAbility(CombatContext const& ctx, AbilityDescriptor const& desc, Unit* target)
    {
        if (!target)
            return -1.0f;

        float targetHpPct = target->GetHealthPct();
        if (targetHpPct < desc.minTargetHpPct || targetHpPct > desc.maxTargetHpPct)
            return -1.0f;

        if (ctx.botHpPct < desc.minSelfHpPct || ctx.botHpPct > desc.maxSelfHpPct)
            return -1.0f;

        if (desc.minInjuredAllies > 0 && ctx.injuredAllyCount < desc.minInjuredAllies)
            return -1.0f;

        float score = desc.baseScore;

        // Tag-driven contextual utility bonuses -- independent `if`s, not an else-if chain: an
        // ability can legitimately carry more than one of these (e.g. EmergencyHeal|DirectHeal,
        // which used to only ever score as EmergencyHeal and silently lose its DirectHeal
        // contribution), and each tag's own bonus should still apply. See item 16 of the
        // combat-engine rework. Interrupt/Taunt/Execute stay hard disqualifiers when tagged --
        // every profile observed tags them alone, purpose-built for that one situation, so
        // failing their condition means this entry has nothing else useful to contribute.
        if (HasTag(desc.tags, AbilityTag::EmergencyHeal))
        {
            // Highest priority: explosive bonus as HP drops below 40%
            score += (40.0f - targetHpPct) * 15.0f;
            if (target == ctx.tankAlly)
                score += 50.0f;
        }
        if (HasTag(desc.tags, AbilityTag::DirectHeal))
        {
            score += (100.0f - targetHpPct) * 2.0f;
            if (target == ctx.tankAlly)
                score += 30.0f;
        }
        if (HasTag(desc.tags, AbilityTag::PeriodicHeal))
        {
            // HoT priority: bonus for maintaining on tank and damaged targets
            if (target == ctx.tankAlly)
                score += 35.0f;
            score += (100.0f - targetHpPct) * 0.8f;
        }
        if (HasTag(desc.tags, AbilityTag::AoEHeal))
        {
            score += static_cast<float>(ctx.injuredAllyCount) * 25.0f;
        }
        if (HasTag(desc.tags, AbilityTag::Interrupt))
        {
            if (!ctx.victimIsCastingInterruptible)
                return -1.0f; // Do not waste kick when nothing is casting
            score += 500.0f;
        }
        if (HasTag(desc.tags, AbilityTag::Taunt))
        {
            if (!ctx.victimTargetingNonTank)
                return -1.0f; // Do not waste taunt if already holding aggro
            score += 400.0f;
        }
        if (HasTag(desc.tags, AbilityTag::Execute))
        {
            if (targetHpPct > 20.0f)
                return -1.0f;
            score += 150.0f;
        }
        if (HasTag(desc.tags, AbilityTag::AoEDamage))
        {
            // Hard eligibility floor (item 7, Phase 2 fixup): below minAoETargets (default 3),
            // an AoE-tagged ability is disqualified outright rather than merely scoring low --
            // a flat count-based bonus alone still let a high-baseScore AoE entry (e.g. Multi-
            // Shot at 205) outscore a genuine single-target one (Aimed Shot at 195) against a
            // single enemy, since the bonus only needed to be non-negative to tip the balance.
            // Gated on engagedEnemyCount, not the wider nearbyEnemyCount (review finding #1 on
            // the Phase 2 fixup pass) -- nearbyEnemyCount counts every attackable unit in range
            // regardless of whether it's part of this fight, so an idle bystander pack standing
            // near the real 1-mob fight could otherwise satisfy the floor on its own and pull
            // them in.
            if (ctx.engagedEnemyCount < desc.minAoETargets)
                return -1.0f;
            // Above the floor, scales with the real engaged-enemy count so a genuine pack still
            // outscores single-target ones, more so for a bigger pack (5+ "high-value" AoE).
            score += static_cast<float>(ctx.engagedEnemyCount) * 20.0f;
        }
        if (HasTag(desc.tags, AbilityTag::DefensiveCD))
        {
            // Contextual bonus/penalty (item 19, Phase 2) on top of the HP-threshold eligibility
            // gate already applied above -- a real incoming-damage trend or a dangerous boss cast
            // targeting the bot makes this much more urgent than the flat HP check alone; a dip
            // that's already stopped hurting the bot (enemy nearly dead, no incoming damage)
            // makes it less so, even while still under the HP threshold.
            if (ctx.worthDefensiveCooldown)
                score += 200.0f;
            else
                score *= 0.5f;
        }

        // Custom scriptable scoring callback
        if (desc.customScorer)
        {
            float customBonus = desc.customScorer(ctx, desc);
            if (customBonus < 0.0f)
                return -1.0f;
            score += customBonus;
        }

        // Cooldown fight-value gate (item 14/#9): heavily deprioritize -- not hard-disqualify,
        // so a class whose only usable ability happens to be tagged OffensiveCD still eventually
        // fires it rather than going silent -- spending a long cooldown on a fight that doesn't
        // warrant it (a trash mob, or a target already about to die on its own).
        if (HasTag(desc.tags, AbilityTag::OffensiveCD) && !ctx.worthOffensiveCooldown)
            score *= 0.15f;

        // Resource management soft floor (item 13, Phase 2): once power is below this ability's
        // own reserve threshold, penalize it in proportion to how inefficient it is
        // (resourceEfficiency < 1) and how deep into the reserve the bot already is -- an
        // efficient option (>= 1.0, the default) takes no penalty at all, so it naturally
        // outscores a penalized wasteful one without needing a separate "low resource" rotation.
        if (desc.reservePowerPct > 0.0f && ctx.botPowerPct < desc.reservePowerPct)
        {
            float depth = 1.0f - (ctx.botPowerPct / desc.reservePowerPct); // 0 at the floor, ->1 at 0 power
            float inefficiencyPenalty = std::max(0.0f, 1.0f - desc.resourceEfficiency);
            score *= std::max(0.15f, 1.0f - depth * inefficiencyPenalty);
        }

        return score;
    }

    BotAction ActionEvaluator::EvaluateBestAction(CombatContext const& ctx, std::vector<AbilityDescriptor> const& abilities)
    {
        BotAction bestAction;
        float bestScore = 0.0f;

        for (AbilityDescriptor const& desc : abilities)
        {
            // Resolved before target selection (review finding #3 on the Phase 2 fixup pass) so
            // the ally-triage target types can filter candidates by THIS spell's real range
            // instead of picking the single best-urgency ally group-wide and only discovering
            // it's unreachable once CanCast rejects it -- see ResolveTarget's own comment.
            uint32 resolvedSpellId = SpellResolver::ResolveSpell(ctx.bot, desc.rootSpellId);
            if (!resolvedSpellId)
                continue;

            Unit* target = ResolveTarget(ctx, desc.targetType, desc, resolvedSpellId);
            if (!target || !target->IsAlive())
                continue;

            if (!CanCast(ctx, desc, resolvedSpellId, target))
                continue;

            float score = ScoreAbility(ctx, desc, target);
            if (score <= 0.0f)
                continue;

            if (score > bestScore)
            {
                bestScore = score;
                bestAction.spellId = resolvedSpellId;
                bestAction.rootSpellId = desc.rootSpellId;
                bestAction.target = target;
                bestAction.score = score;
                bestAction.tags = desc.tags;
                bestAction.name = desc.name;
                bestAction.reason = "highest utility score";
                bestAction.internalThrottleMs = desc.internalThrottleMs;
            }
        }

        return bestAction;
    }
}
