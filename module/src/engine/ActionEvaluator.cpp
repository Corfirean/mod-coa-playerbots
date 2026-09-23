/*
 * mod-coa-playerbots
 *
 * Data-Driven Combat AI Framework: ActionEvaluator implementation
 */

#include "engine/ActionEvaluator.h"
#include "engine/CombatResource.h"
#include "engine/HealEvaluator.h"
#include "engine/SpellPredicates.h"
#include "engine/SpellResolver.h"
#include "engine/SpecStrategyRegistry.h"
#include "engine/TargetEvaluator.h"
#include "BotClassRotations.h"
#include "Creature.h"
#include "Group.h"
#include "ObjectAccessor.h"
#include "Pet.h"
#include "Player.h"
#include "SpellAuras.h"
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
                if (!ctx.victim || !ctx.victim->IsAlive())
                    return nullptr;
                return TargetEvaluator::FindBestAoECluster(ctx.bot, ctx.victim);
            }

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
                return SelectBestReachableAlly(ctx, desc, spellInfo, false);
            }

            case TargetType::AnyInjuredAlly:
            {
                SpellInfo const* spellInfo = resolvedSpellId ? sSpellMgr->GetSpellInfo(resolvedSpellId) : nullptr;
                return SelectBestReachableAlly(ctx, desc, spellInfo, false);
            }

            case TargetType::PartyMissingBuff:
            {
                uint32 auraToCheck = desc.targetAuraId ? desc.targetAuraId : (desc.rootSpellId ? desc.rootSpellId : resolvedSpellId);
                if (Group const* group = ctx.bot->GetGroup())
                {
                    for (GroupReference const* ref = group->GetFirstMember(); ref; ref = ref->next())
                    {
                        Player* member = ref->GetSource();
                        if (member && member->IsAlive() && member->IsInWorld() && member->GetMap() == ctx.bot->GetMap())
                        {
                            if (!member->HasAura(auraToCheck))
                                return member;
                        }
                    }
                }
                return ctx.bot->HasAura(auraToCheck) ? nullptr : ctx.bot;
            }

            default:
                return nullptr;
        }
    }

    bool ActionEvaluator::ValidateAction(CombatContext const& ctx, BotAction const& action, AbilityDescriptor const* desc)
    {
        Player* bot = ctx.bot;
        if (!bot || !action.target || !action.spellId)
            return false;

        if (action.rootSpellId && IsThrottled(bot->GetGUID(), action.rootSpellId))
            return false;

        SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(action.spellId);
        if (!spellInfo)
            return false;

        if (IsSpellInFailureCooldown(bot->GetGUID(), action.spellId))
            return false;

        if (bot->HasSpellCooldown(action.spellId))
            return false;

        if (WouldAoEHitBreakableCrowdControl(bot, action.target, spellInfo))
            return false;

        // Resource check through CombatResourceEvaluator
        if (!CombatResourceEvaluator::CanAfford(ctx.resources, bot, action.spellId))
            return false;

        // Range check
        bool positiveRange = spellInfo->IsPositive();
        if (action.target != bot)
        {
            float dist = bot->GetDistance(action.target);
            float maxRange = spellInfo->GetMaxRange(positiveRange, bot);
            if (maxRange > 0.0f && dist > maxRange)
                return false;

            float minRange = spellInfo->GetMinRange(positiveRange);
            if (minRange > 0.0f && bot->IsWithinRange(action.target, minRange + bot->GetMeleeRange(action.target)))
                return false;
        }

        // If descriptor provided, run its specific checks
        if (desc)
        {
            if (desc->stateRequirement == StateRequirement::EmergencyOnly)
            {
                if (ctx.botHpPct > 35.0f && (!ctx.lowestAlly || ctx.lowestAllyHpPct > 35.0f))
                    return false;
            }

            if (desc->minPowerPct > 0.0f && ctx.botPowerPct < desc->minPowerPct)
                return false;
        }

        return true;
    }

    bool ActionEvaluator::CanCast(CombatContext const& ctx, AbilityDescriptor const& desc, uint32 resolvedSpellId, Unit* target)
    {
        Player* bot = ctx.bot;
        if (!bot || !target || !resolvedSpellId)
            return false;

        if (IsThrottled(bot->GetGUID(), desc.rootSpellId))
            return false;

        SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(resolvedSpellId);
        if (!spellInfo)
            return false;

        if (IsSpellInFailureCooldown(bot->GetGUID(), resolvedSpellId))
            return false;

        if (bot->HasSpellCooldown(resolvedSpellId))
            return false;

        // Breakable CC protection
        if (WouldAoEHitBreakableCrowdControl(bot, target, spellInfo))
            return false;

        // State requirement check
        if (desc.stateRequirement == StateRequirement::EmergencyOnly)
        {
            if (ctx.botHpPct > 35.0f && (!ctx.lowestAlly || ctx.lowestAllyHpPct > 35.0f))
                return false;
        }

        // Persistent entity tracking (Pets, Minions, Turrets, Wards)
        if (desc.trackedEntityType == TrackedEntityType::Pet)
        {
            if (bot->GetPet())
                return false;
        }
        else if (desc.trackedEntityType == TrackedEntityType::Turret ||
                 desc.trackedEntityType == TrackedEntityType::Ward ||
                 desc.trackedEntityType == TrackedEntityType::Minion)
        {
            if (desc.trackedEntityEntry != 0)
            {
                uint8 liveCount = 0;
                for (Unit* controlled : bot->m_Controlled)
                {
                    if (controlled && controlled->IsAlive() && controlled->GetEntry() == desc.trackedEntityEntry)
                    {
                        liveCount++;
                    }
                }
                if (liveCount >= (desc.maxActiveEntities ? desc.maxActiveEntities : 1))
                    return false;
            }
        }

        // Caster aura state & requirements
        if (desc.requiredAuraOnCaster && !bot->HasAura(desc.requiredAuraOnCaster))
            return false;
        if (desc.missingAuraOnCaster && bot->HasAura(desc.missingAuraOnCaster))
            return false;

        if (desc.casterAuraId)
        {
            if (Aura* cAura = bot->GetAura(desc.casterAuraId))
            {
                if (desc.refreshCasterBelowMs > 0 && cAura->GetDuration() > static_cast<int32>(desc.refreshCasterBelowMs))
                    return false;
                if (desc.refreshCasterBelowStacks > 0 && cAura->GetStackAmount() >= desc.refreshCasterBelowStacks)
                    return false;
            }
        }

        if (spellInfo->CasterAuraState && !bot->HasAuraState(AuraStateType(spellInfo->CasterAuraState)))
            return false;
        if (spellInfo->CasterAuraSpell && !bot->HasAura(spellInfo->CasterAuraSpell))
            return false;

        // Target aura requirements with refresh policies
        uint32 auraIdToCheck = desc.targetAuraId ? desc.targetAuraId : resolvedSpellId;
        if (desc.requireAuraMissingOnTarget)
        {
            if (Aura* aura = target->GetAura(auraIdToCheck, bot->GetGUID()))
            {
                if (desc.refreshBelowMs > 0)
                {
                    if (aura->GetDuration() > static_cast<int32>(desc.refreshBelowMs))
                        return false;
                }
                else if (desc.refreshBelowStacks > 0)
                {
                    if (aura->GetStackAmount() >= desc.refreshBelowStacks)
                        return false;
                }
                else
                {
                    return false;
                }
            }
        }

        if (spellInfo->TargetAuraState && !target->HasAuraState(AuraStateType(spellInfo->TargetAuraState)))
            return false;
        if (spellInfo->TargetAuraSpell && !target->HasAura(spellInfo->TargetAuraSpell))
            return false;

        // Resource management hard floor
        if (desc.minPowerPct > 0.0f && ctx.botPowerPct < desc.minPowerPct)
            return false;

        // Universal resource-management engine
        if (!CombatResourceEvaluator::CanAfford(ctx.resources, bot, resolvedSpellId))
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

    float ActionEvaluator::ScoreAbility(CombatContext const& ctx, AbilityDescriptor const& desc, Unit* target, SpecStrategy const* strategy, CombatPhase phase)
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
                return -1.0f;
            score += 500.0f;
        }
        if (HasTag(desc.tags, AbilityTag::Taunt))
        {
            if (!ctx.victimTargetingNonTank)
                return -1.0f;
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
            if (ctx.engagedEnemyCount < desc.minAoETargets)
                return -1.0f;
            score += static_cast<float>(ctx.engagedEnemyCount) * 20.0f;
        }
        if (HasTag(desc.tags, AbilityTag::DefensiveCD))
        {
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

        // Cooldown fight-value gate
        if (HasTag(desc.tags, AbilityTag::OffensiveCD) && !ctx.worthOffensiveCooldown)
            score *= 0.15f;

        // Resource management soft floor
        if (desc.reservePowerPct > 0.0f && ctx.botPowerPct < desc.reservePowerPct)
        {
            float depth = 1.0f - (ctx.botPowerPct / desc.reservePowerPct);
            float inefficiencyPenalty = std::max(0.0f, 1.0f - desc.resourceEfficiency);
            score *= std::max(0.15f, 1.0f - depth * inefficiencyPenalty);
        }

        // Defensive reserve protection: prevent offensive spenders from starving active mitigation
        if (strategy && !strategy->resourcePolicies.empty())
        {
            if (!HasTag(desc.tags, AbilityTag::DefensiveCD) && !HasTag(desc.tags, AbilityTag::EmergencyHeal) && !HasTag(desc.tags, AbilityTag::Taunt))
            {
                for (ResourcePolicy const& pol : strategy->resourcePolicies)
                {
                    if (pol.reserveForDefensive && pol.defensiveReserve > 0)
                    {
                        CombatResourceState const* st = ctx.resources.Find(pol.key);
                        if (st)
                        {
                            // Calculate projected reserve: have - consumption
                            int32 consumptionAmount = 0;
                            uint32 resolvedSpellId = SpellResolver::ResolveSpell(ctx.bot, desc.rootSpellId);
                            if (resolvedSpellId)
                            {
                                for (auto const& req : CombatResourceEvaluator::ResolveRequirements(ctx.bot, resolvedSpellId))
                                {
                                    if (req.key == pol.key && req.activeForBot)
                                    {
                                        consumptionAmount += req.amount;
                                    }
                                }
                            }
                            int32 projected = st->current - consumptionAmount;
                            if (projected < pol.defensiveReserve)
                            {
                                score *= 0.1f;
                                break;
                            }
                        }
                    }
                }
            }
        }

        // Builder bonus during recovery or when low on resources
        if (phase == CombatPhase::Recovery || ctx.botPowerPct < 30.0f)
        {
            uint32 resolvedSpellId = SpellResolver::ResolveSpell(ctx.bot, desc.rootSpellId);
            if (resolvedSpellId)
            {
                auto const& gains = CombatResourceEvaluator::ResolveGains(ctx.classId, resolvedSpellId);
                if (!gains.empty())
                {
                    score += 100.0f;
                }
            }
        }

        // Phase-specific adjustments
        if (phase == CombatPhase::Recovery)
        {
            if (HasTag(desc.tags, AbilityTag::Filler))
                score += 150.0f;
            else if (HasTag(desc.tags, AbilityTag::DefensiveCD) || HasTag(desc.tags, AbilityTag::EmergencyHeal))
                score += 100.0f;
            else if (HasTag(desc.tags, AbilityTag::OffensiveCD))
                score *= 0.1f;
        }
        else if (phase == CombatPhase::Opener)
        {
            if (HasTag(desc.tags, AbilityTag::Taunt))
                score += 200.0f;
            else if (HasTag(desc.tags, AbilityTag::MeleeAttack) || HasTag(desc.tags, AbilityTag::RangedAttack) || HasTag(desc.tags, AbilityTag::Buff))
                score += 50.0f;
        }
        else if (phase == CombatPhase::Burst)
        {
            if (HasTag(desc.tags, AbilityTag::OffensiveCD))
                score += 250.0f;
        }

        return score;
    }

    BotAction ActionEvaluator::EvaluateBestAction(CombatContext const& ctx, std::vector<AbilityDescriptor> const& abilities)
    {
        SpecStrategy const* strategy = SpecStrategyRegistry::FindStrategy(ctx.classId, ctx.activeSpec, ctx.role);
        return EvaluateBestAction(ctx, abilities, strategy);
    }

    BotAction ActionEvaluator::EvaluateBestAction(CombatContext const& ctx, std::vector<AbilityDescriptor> const& abilities, SpecStrategy const* strategy)
    {
        BotAction bestAction;
        float bestScore = 0.0f;
        uint32 now = getMSTime();

        SpecStrategyRuntime& runtime = SpecStrategyRegistry::GetRuntime(ctx.bot->GetGUID());
        if (ctx.bot->IsInCombat())
        {
            if (runtime.combatStartMs == 0)
                runtime.combatStartMs = now;
        }
        else
        {
            runtime.combatStartMs = 0;
            runtime.successfulCastsCount = 0;
            runtime.successfulCombatCasts = 0;
            runtime.openerCompleted = false;
            runtime.burstWindowActive = false;
            runtime.burstStartedMs = 0;
        }

        // 1. Form / Stance readiness check and form dancing state machine
        BotAction stateRecoveryAction;
        CombatStateStatus stateStatus = SpecStrategyRegistry::EvaluateCombatState(ctx.bot, strategy, ctx, &stateRecoveryAction);

        if (stateRecoveryAction.IsValid())
        {
            return stateRecoveryAction;
        }

        // If missing baseline state or setup is required and no recovery action was generated,
        // do NOT allow normal rotation
        if (stateStatus == CombatStateStatus::NeedEnterBaseline || stateStatus == CombatStateStatus::SetupRequired || stateStatus == CombatStateStatus::ReturningToBaseline)
        {
            return bestAction;
        }

        // 2. Combat Phase Detection
        CombatPhase phase = CombatPhase::SingleTarget;
        // Priority 1: Emergency always preempts normal rotation and opener
        if (ctx.botHpPct < 25.0f || (ctx.lowestAlly && ctx.lowestAllyHpPct < 25.0f))
        {
            phase = CombatPhase::Emergency;
        }
        // Priority 2: Opener phase during first 4 seconds or first 3 casts
        else if (ctx.bot->IsInCombat() && runtime.combatStartMs != 0 && (now < runtime.combatStartMs + 4000) && runtime.successfulCombatCasts < 3 && !runtime.openerCompleted)
        {
            phase = CombatPhase::Opener;
        }
        else
        {
            runtime.openerCompleted = true;
            if (strategy && strategy->detectPhase)
            {
                phase = strategy->detectPhase(ctx);
            }
            else
            {
                // Burst window management: 15s window when triggered
                if (ctx.worthOffensiveCooldown && !runtime.burstWindowActive)
                {
                    runtime.burstWindowActive = true;
                    runtime.burstStartedMs = now;
                }
                else if (runtime.burstWindowActive && now >= runtime.burstStartedMs + 15000)
                {
                    runtime.burstWindowActive = false;
                }

                if (runtime.burstWindowActive)
                    phase = CombatPhase::Burst;
                else if (strategy && strategy->recoveryThreshold > 0.0f && ctx.botPowerPct < strategy->recoveryThreshold)
                    phase = CombatPhase::Recovery;
                else if (ctx.targetHpPct < 20.0f)
                    phase = CombatPhase::Execute;
                else if (ctx.engagedEnemyCount >= 3)
                    phase = CombatPhase::AoE;
                else if (ctx.engagedEnemyCount == 2)
                    phase = CombatPhase::Cleave;
                else
                    phase = CombatPhase::SingleTarget;
            }
        }
        runtime.phase = phase;

        for (AbilityDescriptor const& desc : abilities)
        {
            // State requirement gating
            if (stateStatus != CombatStateStatus::Ready)
            {
                if (desc.stateRequirement == StateRequirement::BaselineOnly)
                    continue;
            }
            if (stateStatus == CombatStateStatus::TemporaryAlternate)
            {
                if (desc.stateRequirement != StateRequirement::AllowedInTemporary &&
                    desc.stateRequirement != StateRequirement::EmergencyOnly &&
                    desc.stateRequirement != StateRequirement::Any)
                    continue;
            }

            uint32 resolvedSpellId = SpellResolver::ResolveSpell(ctx.bot, desc.rootSpellId);
            if (!resolvedSpellId)
                continue;

            Unit* target = ResolveTarget(ctx, desc.targetType, desc, resolvedSpellId);
            if (!target || !target->IsAlive())
                continue;

            if (!CanCast(ctx, desc, resolvedSpellId, target))
                continue;

            float score = ScoreAbility(ctx, desc, target, strategy, phase);
            if (score <= 0.0f)
                continue;

            // Apply phase score modifiers from strategy
            if (strategy)
            {
                auto it = strategy->phaseModifiers.find(phase);
                if (it != strategy->phaseModifiers.end())
                {
                    for (PhaseScoreModifier const& mod : it->second)
                    {
                        if (HasTag(desc.tags, mod.tag))
                        {
                            score = (score * mod.multiplier) + mod.additive;
                        }
                    }
                }
            }

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
