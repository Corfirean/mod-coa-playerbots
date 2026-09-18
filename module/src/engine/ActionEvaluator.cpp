/*
 * mod-coa-playerbots
 *
 * Data-Driven Combat AI Framework: ActionEvaluator implementation
 */

#include "engine/ActionEvaluator.h"
#include "engine/SpellResolver.h"
#include "BotClassRotations.h"
#include "Group.h"
#include "Player.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "Timer.h"
#include <unordered_map>

namespace BotAI
{
    namespace
    {
        // [botGuid][rootSpellId] -> expiryMSTime
        std::unordered_map<ObjectGuid, std::unordered_map<uint32, uint32>> s_internalThrottles;
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

    Unit* ActionEvaluator::ResolveTarget(CombatContext const& ctx, TargetType targetType, AbilityDescriptor const& desc)
    {
        switch (targetType)
        {
            case TargetType::Self:
                return ctx.bot;

            case TargetType::CurrentTarget:
            case TargetType::AreaHostile:
                return (ctx.victim && ctx.victim->IsAlive()) ? ctx.victim : nullptr;

            case TargetType::LowestHealthAlly:
                return ctx.lowestAlly ? ctx.lowestAlly : ctx.bot;

            case TargetType::TankAlly:
                return ctx.tankAlly ? ctx.tankAlly : ctx.lowestAlly;

            case TargetType::AnyInjuredAlly:
            {
                if (Group const* group = ctx.bot->GetGroup())
                {
                    for (GroupReference const* ref = group->GetFirstMember(); ref; ref = ref->next())
                    {
                        Player* member = ref->GetSource();
                        if (!member || !member->IsAlive() || !member->IsInWorld())
                            continue;

                        if (member->GetHealthPct() <= desc.maxTargetHpPct &&
                            (!desc.requireAuraMissingOnTarget || !member->HasAura(desc.rootSpellId, ctx.bot->GetGUID())))
                        {
                            return member;
                        }
                    }
                }
                return (ctx.bot->GetHealthPct() <= desc.maxTargetHpPct) ? ctx.bot : nullptr;
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

        // Tag-driven contextual utility bonuses
        if (HasTag(desc.tags, AbilityTag::EmergencyHeal))
        {
            // Highest priority: explosive bonus as HP drops below 40%
            score += (40.0f - targetHpPct) * 15.0f;
            if (target == ctx.tankAlly)
                score += 50.0f;
        }
        else if (HasTag(desc.tags, AbilityTag::DirectHeal))
        {
            score += (100.0f - targetHpPct) * 2.0f;
            if (target == ctx.tankAlly)
                score += 30.0f;
        }
        else if (HasTag(desc.tags, AbilityTag::PeriodicHeal))
        {
            // HoT priority: bonus for maintaining on tank and damaged targets
            if (target == ctx.tankAlly)
                score += 35.0f;
            score += (100.0f - targetHpPct) * 0.8f;
        }
        else if (HasTag(desc.tags, AbilityTag::AoEHeal))
        {
            score += static_cast<float>(ctx.injuredAllyCount) * 25.0f;
        }
        else if (HasTag(desc.tags, AbilityTag::Interrupt))
        {
            if (!ctx.victimIsCastingInterruptible)
                return -1.0f; // Do not waste kick when nothing is casting
            score += 500.0f;
        }
        else if (HasTag(desc.tags, AbilityTag::Taunt))
        {
            if (!ctx.victimTargetingNonTank)
                return -1.0f; // Do not waste taunt if already holding aggro
            score += 400.0f;
        }
        else if (HasTag(desc.tags, AbilityTag::Execute))
        {
            if (targetHpPct > 20.0f)
                return -1.0f;
            score += 150.0f;
        }

        // Custom scriptable scoring callback
        if (desc.customScorer)
        {
            float customBonus = desc.customScorer(ctx, desc);
            if (customBonus < 0.0f)
                return -1.0f;
            score += customBonus;
        }

        return score;
    }

    BotAction ActionEvaluator::EvaluateBestAction(CombatContext const& ctx, std::vector<AbilityDescriptor> const& abilities)
    {
        BotAction bestAction;
        float bestScore = 0.0f;

        for (AbilityDescriptor const& desc : abilities)
        {
            Unit* target = ResolveTarget(ctx, desc.targetType, desc);
            if (!target || !target->IsAlive())
                continue;

            uint32 resolvedSpellId = SpellResolver::ResolveSpell(ctx.bot, desc.rootSpellId);
            if (!resolvedSpellId)
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
                bestAction.target = target;
                bestAction.score = score;
                bestAction.tags = desc.tags;
                bestAction.name = desc.name;
                bestAction.reason = "highest utility score";
            }
        }

        return bestAction;
    }
}
