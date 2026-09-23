/*
 * mod-coa-playerbots
 *
 * Data-Driven Combat AI Framework: SpecStrategyRegistry implementation
 */

#include "engine/SpecStrategyRegistry.h"
#include "engine/ActionEvaluator.h"
#include "engine/CombatContext.h"
#include "engine/CombatResource.h"
#include "engine/SpellResolver.h"
#include "profiles/ProfileRegistry.h"
#include "ClassSpecRoles.h"
#include "Group.h"
#include "Log.h"
#include "Player.h"
#include "SpellMgr.h"
#include "Timer.h"
#include <algorithm>
#include <unordered_map>
#include <vector>

namespace BotAI
{
    namespace
    {
        std::vector<SpecStrategy> s_strategies;
        std::unordered_map<ObjectGuid, SpecStrategyRuntime> s_runtimes;
    }

    bool SpecStrategy::CanUseLegacyFallback(CombatContext const& ctx) const
    {
        if (!ctx.bot)
            return false;

        SpecStrategyRuntime const& runtime = SpecStrategyRegistry::GetRuntime(ctx.bot->GetGUID());
        if (runtime.lastStateStatus == CombatStateStatus::NeedEnterBaseline ||
            runtime.lastStateStatus == CombatStateStatus::SetupRequired ||
            runtime.lastStateStatus == CombatStateStatus::ReturningToBaseline)
        {
            return false;
        }

        // 1. Mandatory form/state global check:
        // If the bot lacks its baseline form and is not currently in an allowed
        // temporary state, legacy fallback rotations MUST NOT execute (they would
        // blindly cast out-of-form spells and break spec contracts).
        if (requiredState.formSpellId != 0 || requiredState.formAuraId != 0)
        {
            uint32 baselineAura = requiredState.formAuraId ? requiredState.formAuraId : requiredState.formSpellId;
            uint32 formSpellId = requiredState.formSpellId ? requiredState.formSpellId : requiredState.formAuraId;

            // Only enforce if the bot actually knows this form spell
            if (ctx.bot->HasSpell(formSpellId) || SpellResolver::ResolveSpell(ctx.bot, formSpellId) != 0)
            {
                bool hasBaseline = ctx.bot->HasAura(baselineAura);
                if (!hasBaseline)
                {
                    if (!allowLegacyInTemporaryState)
                        return false;

                    bool inAllowedTemporary = false;
                    for (auto const& rule : requiredState.transitionRules)
                    {
                        if (rule.targetAuraId && ctx.bot->HasAura(rule.targetAuraId))
                        {
                            inAllowedTemporary = true;
                            break;
                        }
                    }
                    if (!inAllowedTemporary)
                    {
                        for (uint32 altAura : requiredState.alternateFormAuras)
                        {
                            if (ctx.bot->HasAura(altAura))
                            {
                                inAllowedTemporary = true;
                                break;
                            }
                        }
                    }

                    if (!inAllowedTemporary)
                        return false;
                }
            }
        }

        // 2. Setup requirement check (e.g. Tinker Mechsuit needing Scrap)
        if (requiredState.isSetupNeeded && requiredState.isSetupNeeded(ctx.bot, ctx))
            return false;

        return true;
    }

    void SpecStrategyRegistry::RegisterStrategy(SpecStrategy strategy)
    {
        s_strategies.push_back(std::move(strategy));
    }

    SpecStrategy const* SpecStrategyRegistry::FindStrategy(uint8 classId, uint32 specId, BotRole role)
    {
        ProfileRegistry::Initialize();
        for (SpecStrategy const& s : s_strategies)
        {
            if (s.classId == classId && s.specId == specId && s.role == role)
                return &s;
        }
        return nullptr;
    }

    SpecStrategy const* SpecStrategyRegistry::FindStrategy(uint8 classId, uint32 specId)
    {
        ProfileRegistry::Initialize();
        for (SpecStrategy const& s : s_strategies)
        {
            if (s.classId == classId && s.specId == specId)
                return &s;
        }
        return nullptr;
    }

    bool SpecStrategyRegistry::HasStrategy(uint8 classId, uint32 specId, BotRole role)
    {
        return FindStrategy(classId, specId, role) != nullptr;
    }

    std::vector<SpecStrategy> const& SpecStrategyRegistry::GetAllStrategies()
    {
        ProfileRegistry::Initialize();
        return s_strategies;
    }

    SpecStrategyRuntime& SpecStrategyRegistry::GetRuntime(ObjectGuid botGuid)
    {
        return s_runtimes[botGuid];
    }

    void SpecStrategyRegistry::ForgetBot(ObjectGuid botGuid)
    {
        s_runtimes.erase(botGuid);
    }

    void SpecStrategyRegistry::ClearAllRuntimes()
    {
        s_runtimes.clear();
    }

    void SpecStrategyRegistry::OnActionCastResult(Player* bot, BotAction const& action, bool success)
    {
        if (!bot)
            return;

        SpecStrategyRuntime& runtime = GetRuntime(bot->GetGUID());
        uint32 now = getMSTime();

        if (success)
        {
            runtime.successfulCastsCount++;
            if (bot->IsInCombat())
                runtime.successfulCombatCasts++;

            // If this was a pending form transition spell, update timestamps and clear pending
            if (runtime.pendingStateSpellId != 0 &&
                (action.spellId == runtime.pendingStateSpellId || action.rootSpellId == runtime.pendingStateSpellId))
            {
                runtime.lastFormTransitionMs = now;
                runtime.lastSuccessfulFormCastMs = now;
                runtime.successfulStateActions++;
                runtime.pendingStateSpellId = 0;
                runtime.pendingStateStartedMs = 0;
            }
        }
        else
        {
            if (runtime.pendingStateSpellId != 0 &&
                (action.spellId == runtime.pendingStateSpellId || action.rootSpellId == runtime.pendingStateSpellId))
            {
                runtime.pendingStateSpellId = 0;
                runtime.pendingStateStartedMs = 0;
            }
        }
    }

    CombatStateStatus SpecStrategyRegistry::EvaluateCombatState(Player* bot, SpecStrategy const* strategy, CombatContext const& ctx, BotAction* outRecoveryAction)
    {
        if (!bot || !strategy)
            return CombatStateStatus::Ready;

        uint32 formSpellId = strategy->requiredState.formSpellId ? strategy->requiredState.formSpellId : strategy->requiredState.formAuraId;
        uint32 baselineAura = strategy->requiredState.formAuraId ? strategy->requiredState.formAuraId : strategy->requiredState.formSpellId;

        // If no required state is defined, bot is inherently ready
        if (formSpellId == 0 && baselineAura == 0)
            return CombatStateStatus::Ready;

        // If bot does not know the form spell yet (e.g. low level), do not block
        if (!bot->HasSpell(formSpellId) && SpellResolver::ResolveSpell(bot, formSpellId) == 0)
            return CombatStateStatus::Ready;

        uint32 now = getMSTime();
        SpecStrategyRuntime& runtime = GetRuntime(bot->GetGUID());

        // Check pending transition timeout (2.5 seconds)
        if (runtime.pendingStateSpellId != 0 && now >= runtime.pendingStateStartedMs + 2500)
        {
            runtime.pendingStateSpellId = 0;
            runtime.pendingStateStartedMs = 0;
        }

        // 1. Check if bot has baseline form active
        bool hasBaseline = bot->HasAura(baselineAura);
        if (hasBaseline)
        {
            runtime.lastBaselineStateMs = now;
            runtime.pendingStateSpellId = 0;
            runtime.pendingStateStartedMs = 0;

            // Check if any structured transition rule triggers entry into a temporary form
            for (auto const& rule : strategy->requiredState.transitionRules)
            {
                if (rule.shouldEnter && rule.shouldEnter(bot, ctx))
                {
                    if (now >= runtime.lastFormTransitionMs + rule.minHoldTimeMs)
                    {
                        if (outRecoveryAction && rule.targetSpellId)
                        {
                            outRecoveryAction->spellId = SpellResolver::ResolveSpell(bot, rule.targetSpellId);
                            outRecoveryAction->rootSpellId = rule.targetSpellId;
                            outRecoveryAction->target = bot;
                            outRecoveryAction->score = 1200.0f;
                            outRecoveryAction->tags = AbilityTag::Buff;
                            outRecoveryAction->name = "Enter Temporary State";
                            outRecoveryAction->reason = "transition rule triggered";
                        }
                        runtime.pendingStateSpellId = rule.targetSpellId;
                        runtime.pendingStateStartedMs = now;
                        runtime.lastStateStatus = CombatStateStatus::TemporaryAlternate;
                        return CombatStateStatus::TemporaryAlternate;
                    }
                }
            }

            runtime.lastStateStatus = CombatStateStatus::Ready;
            return CombatStateStatus::Ready;
        }

        // 2. Baseline is not active -- check if in an allowed temporary state
        for (auto const& rule : strategy->requiredState.transitionRules)
        {
            if (rule.targetAuraId && bot->HasAura(rule.targetAuraId))
            {
                // In temporary form: check if exit conditions are met
                bool shouldExit = (rule.shouldExit && rule.shouldExit(bot, ctx));
                if (shouldExit && (now >= runtime.lastFormTransitionMs + rule.minHoldTimeMs))
                {
                    if (outRecoveryAction)
                    {
                        outRecoveryAction->spellId = SpellResolver::ResolveSpell(bot, formSpellId);
                        outRecoveryAction->rootSpellId = formSpellId;
                        outRecoveryAction->target = bot;
                        outRecoveryAction->score = 1500.0f;
                        outRecoveryAction->tags = AbilityTag::Buff;
                        outRecoveryAction->name = "Return to Baseline Form";
                        outRecoveryAction->reason = "temporary condition ended";
                    }
                    runtime.pendingStateSpellId = formSpellId;
                    runtime.pendingStateStartedMs = now;
                    runtime.lastStateStatus = CombatStateStatus::ReturningToBaseline;
                    return CombatStateStatus::ReturningToBaseline;
                }

                runtime.lastStateStatus = CombatStateStatus::TemporaryAlternate;
                return CombatStateStatus::TemporaryAlternate;
            }
        }

        // Check alternateFormAuras for backward compatibility
        for (uint32 altAura : strategy->requiredState.alternateFormAuras)
        {
            if (bot->HasAura(altAura))
            {
                runtime.lastStateStatus = CombatStateStatus::TemporaryAlternate;
                return CombatStateStatus::TemporaryAlternate;
            }
        }

        // 3. Neither baseline nor allowed temporary state is present.
        // Check if a prerequisite setup action is needed (e.g. Tinker Mechanics Scrap generation)
        if (strategy->requiredState.isSetupNeeded && strategy->requiredState.isSetupNeeded(bot, ctx))
        {
            if (strategy->requiredState.buildStateRecoveryAction && outRecoveryAction)
            {
                *outRecoveryAction = strategy->requiredState.buildStateRecoveryAction(bot, ctx);
            }
            runtime.lastStateStatus = CombatStateStatus::SetupRequired;
            return CombatStateStatus::SetupRequired;
        }

        // 4. Must enter baseline form immediately
        if (outRecoveryAction)
        {
            outRecoveryAction->spellId = SpellResolver::ResolveSpell(bot, formSpellId);
            outRecoveryAction->rootSpellId = formSpellId;
            outRecoveryAction->target = bot;
            outRecoveryAction->score = 1500.0f;
            outRecoveryAction->tags = AbilityTag::Buff;
            outRecoveryAction->name = "Enter Baseline Form";
            outRecoveryAction->reason = "mandatory baseline state missing";
        }
        runtime.pendingStateSpellId = formSpellId;
        runtime.pendingStateStartedMs = now;
        runtime.lastStateStatus = CombatStateStatus::NeedEnterBaseline;
        return CombatStateStatus::NeedEnterBaseline;
    }

    PrePullResult SpecStrategyRegistry::ExecutePrePullStrategy(Player* bot, Group* group, uint32 diff)
    {
        if (!bot || !bot->IsAlive() || !bot->IsInWorld())
            return PrePullResult::Impossible;

        if (bot->IsInCombat())
            return PrePullResult::Ready;

        // 1. Eating, drinking, or bandaging
        if (bot->HasAuraWithMechanic(1 << MECHANIC_BANDAGE) ||
            bot->HasAura(430) || bot->HasAura(433) || bot->HasAura(10258) || bot->HasAura(22734) ||
            bot->HasAura(27089) || bot->HasAura(34291) || bot->HasAura(43180) || bot->HasAura(43183))
        {
            return PrePullResult::Waiting;
        }

        // 2. Health & resource recovery floor
        if (bot->GetHealthPct() < 60.0f)
            return PrePullResult::Recovering;

        uint32 activeSpec = bot->GetPlayerSetting("core.ascension_active_spec", 0).value;
        BotRole role = BotAI::GetRole(bot->GetGUID());
        SpecStrategy const* strategy = FindStrategy(bot->getClass(), activeSpec, role);
        if (!strategy)
            strategy = FindStrategy(bot->getClass(), activeSpec);

        if (!strategy)
            return PrePullResult::Ready;

        CombatContext ctx = CombatContext::Build(bot, nullptr);
        SpecStrategyRuntime& runtime = GetRuntime(bot->GetGUID());
        uint32 now = getMSTime();

        // 3. Baseline Form / Stance enforcement
        if (strategy->requiredState.formSpellId != 0 || strategy->requiredState.formAuraId != 0)
        {
            uint32 baselineAura = strategy->requiredState.formAuraId ? strategy->requiredState.formAuraId : strategy->requiredState.formSpellId;
            uint32 formSpellId = strategy->requiredState.formSpellId ? strategy->requiredState.formSpellId : strategy->requiredState.formAuraId;

            if (bot->HasSpell(formSpellId) || SpellResolver::ResolveSpell(bot, formSpellId) != 0)
            {
                bool hasBaseline = bot->HasAura(baselineAura);
                if (!hasBaseline)
                {
                    // Special case: Tinker Mechanics (spec 50) Scrap & Mechsuit:
                    // In core, Scrap is gained on FirstSuccessfulDamagingHit in combat. Out-of-combat Scrap generation is impossible.
                    // If Scrap < 10 and Mechsuit is not up, bot is ReadyForOpenerPull (will cast Mechsuit immediately upon first hit).
                    if (bot->getClass() == CLASS_TINKER && activeSpec == 50)
                    {
                        CombatResourceSnapshot snap = CombatResourceEvaluator::BuildSnapshot(bot);
                        CombatResourceState const* scrapState = snap.Find(CombatResourceKey{CombatResourceKind::AuraStack, 0, 801816});
                        int32 scrapCount = scrapState ? scrapState->current : 0;
                        if (scrapCount >= 10)
                        {
                            // Can build Mechsuit immediately
                            uint32 resolvedMechsuit = SpellResolver::ResolveSpell(bot, 92141);
                            if (resolvedMechsuit)
                            {
                                BotAction preAction;
                                preAction.spellId = resolvedMechsuit;
                                preAction.rootSpellId = 92141;
                                preAction.target = bot;
                                preAction.score = 2000.0f;
                                preAction.name = "Build: Mechsuit (PrePull)";
                                if (ActionEvaluator::ValidateAction(ctx, preAction))
                                {
                                    SpellCastResult res = bot->CastSpell(bot, resolvedMechsuit, false);
                                    OnActionCastResult(bot, preAction, res == SPELL_CAST_OK);
                                    if (res == SPELL_CAST_OK)
                                        return PrePullResult::ActionExecuted;
                                }
                            }
                        }
                        else
                        {
                            // Scrap < 10: ready for opener pull (controlled pull to get first hit Scrap)
                        }
                    }
                    else
                    {
                        // Standard baseline form shift (e.g. Fortitude Beetle Form, Vizier Form, Bear Form, Stance)
                        uint32 resolvedForm = SpellResolver::ResolveSpell(bot, formSpellId);
                        if (resolvedForm)
                        {
                            BotAction formAction;
                            formAction.spellId = resolvedForm;
                            formAction.rootSpellId = formSpellId;
                            formAction.target = bot;
                            formAction.score = 2000.0f;
                            formAction.name = "Baseline Form (PrePull)";
                            if (ActionEvaluator::ValidateAction(ctx, formAction))
                            {
                                SpellCastResult res = bot->CastSpell(bot, resolvedForm, false);
                                OnActionCastResult(bot, formAction, res == SPELL_CAST_OK);
                                if (res == SPELL_CAST_OK)
                                    return PrePullResult::ActionExecuted;
                            }
                        }
                        return PrePullResult::Waiting;
                    }
                }
            }
        }

        // 4. Pet / minion preparation (e.g. Houndmaster Whistle)
        if (strategy->isPetReady && !strategy->isPetReady(bot))
        {
            if (bot->getClass() == CLASS_WITCH_HUNTER && activeSpec == 11)
            {
                uint32 resolvedWhistle = SpellResolver::ResolveSpell(bot, 801343);
                if (resolvedWhistle)
                {
                    BotAction petAction;
                    petAction.spellId = resolvedWhistle;
                    petAction.rootSpellId = 801343;
                    petAction.target = bot;
                    petAction.score = 1800.0f;
                    petAction.name = "Call Hound (PrePull)";
                    if (ActionEvaluator::ValidateAction(ctx, petAction))
                    {
                        SpellCastResult res = bot->CastSpell(bot, resolvedWhistle, false);
                        OnActionCastResult(bot, petAction, res == SPELL_CAST_OK);
                        if (res == SPELL_CAST_OK)
                            return PrePullResult::ActionExecuted;
                    }
                }
            }
            return PrePullResult::Waiting;
        }

        return PrePullResult::Ready;
    }

    PullReadinessInfo SpecStrategyRegistry::EvaluatePullReadiness(Player* bot, CombatContext const* ctx)
    {
        PullReadinessInfo info;
        if (!bot || !bot->IsAlive() || !bot->IsInWorld())
        {
            info.result = PullReadinessResult::GroupMemberDead;
            info.reason = "DeadOrNotInWorld";
            return info;
        }

        // Eating, drinking, or resting
        if (bot->HasAuraWithMechanic(1 << MECHANIC_BANDAGE) ||
            bot->HasAura(430) || bot->HasAura(433) || bot->HasAura(10258) || bot->HasAura(22734) ||
            bot->HasAura(27089) || bot->HasAura(34291) || bot->HasAura(43180) || bot->HasAura(43183))
        {
            info.result = PullReadinessResult::GroupMemberResting;
            info.reason = "RestingOrDrinking";
            return info;
        }

        // Critical health (< 50% HP)
        if (bot->GetHealthPct() < 50.0f)
        {
            info.result = PullReadinessResult::Recovering;
            info.reason = "HealthBelow50Pct";
            return info;
        }

        uint32 activeSpec = bot->GetPlayerSetting("core.ascension_active_spec", 0).value;
        BotRole role = BotAI::GetRole(bot->GetGUID());
        SpecStrategy const* strategy = FindStrategy(bot->getClass(), activeSpec, role);

        // Fallback strategy search if role-specific not found
        if (!strategy)
            strategy = FindStrategy(bot->getClass(), activeSpec);

        if (strategy)
        {
            // Form check: must be in baseline form or allowed temporary form
            if (strategy->requiredState.formSpellId != 0 || strategy->requiredState.formAuraId != 0)
            {
                uint32 baselineAura = strategy->requiredState.formAuraId ? strategy->requiredState.formAuraId : strategy->requiredState.formSpellId;
                uint32 formSpellId = strategy->requiredState.formSpellId ? strategy->requiredState.formSpellId : strategy->requiredState.formAuraId;

                if (bot->HasSpell(formSpellId) || SpellResolver::ResolveSpell(bot, formSpellId) != 0)
                {
                    if (!bot->HasAura(baselineAura))
                    {
                        // Tinker Mechanics spec 50: if Scrap < 10, Mechsuit cannot be cast out of combat.
                        // Allow controlled opener pull to get first hit Scrap!
                        if (bot->getClass() == CLASS_TINKER && activeSpec == 50)
                        {
                            CombatResourceSnapshot snap = CombatResourceEvaluator::BuildSnapshot(bot);
                            CombatResourceState const* scrapState = snap.Find(CombatResourceKey{CombatResourceKind::AuraStack, 0, 801816});
                            int32 scrapCount = scrapState ? scrapState->current : 0;
                            if (scrapCount < 10)
                            {
                                // Allowed to pull to gain Scrap
                            }
                            else
                            {
                                info.result = PullReadinessResult::WaitingForForm;
                                info.reason = "MissingMechsuitWithScrapAvailable";
                                return info;
                            }
                        }
                        else
                        {
                            info.result = PullReadinessResult::WaitingForForm;
                            info.reason = "MissingMandatoryForm";
                            return info;
                        }
                    }
                }
            }

            // Power check
            if (strategy->minResourceToEngage > 0.0f)
            {
                Powers pType = bot->getPowerType();
                uint32 maxPower = std::max(1u, bot->GetMaxPower(pType));
                float currentPowerPct = bot->GetPower(pType) * 100.0f / maxPower;
                if (currentPowerPct < strategy->minResourceToEngage)
                {
                    info.result = PullReadinessResult::WaitingForResource;
                    info.reason = "PowerBelowMinToEngage";
                    return info;
                }
            }

            // Multi-resource channel policies
            if (!strategy->resourcePolicies.empty())
            {
                CombatResourceSnapshot snapshot = CombatResourceEvaluator::BuildSnapshot(bot);
                for (ResourcePolicy const& pol : strategy->resourcePolicies)
                {
                    if (pol.minToEngage > 0)
                    {
                        CombatResourceState const* st = snapshot.Find(pol.key);
                        if (!st || st->current < pol.minToEngage)
                        {
                            info.result = PullReadinessResult::WaitingForResource;
                            info.reason = "CustomResourceBelowMinToEngage";
                            return info;
                        }
                    }
                }
            }

            // Pet readiness
            if (strategy->isPetReady && !strategy->isPetReady(bot))
            {
                info.result = PullReadinessResult::WaitingForPet;
                info.reason = "PetNotReady";
                return info;
            }

            // Custom isReadyToPull callback
            if (strategy->isReadyToPull && ctx)
            {
                if (!strategy->isReadyToPull(bot, *ctx))
                {
                    info.result = (role == BotRole::Tank) ? PullReadinessResult::TankNotReady : PullReadinessResult::HealerNotReady;
                    info.reason = "SpecStrategyReadinessFailed";
                    return info;
                }
            }
        }

        info.result = PullReadinessResult::Ready;
        info.reason = "Ready";
        if (role == BotRole::Tank)
            info.tankReady = true;
        if (role == BotRole::Healer)
            info.healerReady = true;
        return info;
    }

    PullReadinessInfo SpecStrategyRegistry::EvaluateGroupPullReadiness(Player* tank, Group* group)
    {
        PullReadinessInfo info;
        if (!tank || !tank->IsAlive())
        {
            info.result = PullReadinessResult::TankNotReady;
            info.reason = "TankDeadOrNull";
            return info;
        }

        // 1. Tank check
        CombatContext tankCtx = CombatContext::Build(tank, nullptr);
        PullReadinessInfo tankInfo = EvaluatePullReadiness(tank, &tankCtx);
        if (!tankInfo.IsReady())
        {
            info.result = PullReadinessResult::TankNotReady;
            info.reason = tankInfo.reason;
            return info;
        }
        info.tankReady = true;

        if (!group)
        {
            info.result = PullReadinessResult::Ready;
            info.reason = "SoloTankReady";
            return info;
        }

        // 2. Scan group members: Healer priority and group readiness
        bool healerFound = false;
        bool healerReady = false;

        for (GroupReference const* ref = group->GetFirstMember(); ref; ref = ref->next())
        {
            Player* member = ref->GetSource();
            if (!member || member == tank)
                continue;

            if (!member->IsAlive())
            {
                info.result = PullReadinessResult::GroupMemberDead;
                info.reason = "GroupMemberDead";
                return info;
            }

            // Check distance to tank (must not be stranded or left behind)
            float dist = tank->GetDistance(member);
            if (dist > 50.0f)
            {
                info.result = PullReadinessResult::GroupMemberFar;
                info.reason = "MemberOutOfRange";
                return info;
            }

            // Check if drinking / resting
            if (member->HasAuraWithMechanic(1 << MECHANIC_BANDAGE) ||
                member->HasAura(430) || member->HasAura(433) || member->HasAura(10258) || member->HasAura(22734) ||
                member->HasAura(27089) || member->HasAura(34291) || member->HasAura(43180) || member->HasAura(43183))
            {
                info.result = PullReadinessResult::GroupMemberResting;
                info.reason = "MemberDrinkingOrResting";
                return info;
            }

            BotRole memberRole = BotAI::GetRole(member->GetGUID());
            if (memberRole == BotRole::Healer)
            {
                healerFound = true;

                // Distance to healer must be within casting range (35 yards)
                if (dist > 35.0f)
                {
                    info.result = PullReadinessResult::HealerNotReady;
                    info.reason = "HealerOutOfRange";
                    return info;
                }

                // Healer health floor
                if (member->GetHealthPct() < 60.0f)
                {
                    info.result = PullReadinessResult::HealerNotReady;
                    info.reason = "HealerHealthBelow60";
                    return info;
                }

                CombatContext healerCtx = CombatContext::Build(member, nullptr);
                PullReadinessInfo healerInfo = EvaluatePullReadiness(member, &healerCtx);
                if (!healerInfo.IsReady())
                {
                    info.result = PullReadinessResult::HealerNotReady;
                    info.reason = healerInfo.reason;
                    return info;
                }

                healerReady = true;
            }
        }

        // If a healer exists in the group, they must be confirmed ready
        if (healerFound && !healerReady)
        {
            info.result = PullReadinessResult::HealerNotReady;
            info.reason = "HealerNotReady";
            return info;
        }

        info.healerReady = healerReady || !healerFound;
        info.result = PullReadinessResult::Ready;
        info.reason = "GroupReady";
        return info;
    }

    void SpecStrategyRegistry::Validate()
    {
        ProfileRegistry::Initialize();
        LOG_INFO("module.coa-playerbots", "SpecStrategyRegistry: Validating strategies census...");

        uint32 strategyCount = static_cast<uint32>(s_strategies.size());
        LOG_INFO("module.coa-playerbots", "SpecStrategyRegistry: Loaded {} registered spec strategies.", strategyCount);

        std::unordered_map<uint64, uint32> registeredKeys;
        for (SpecStrategy const& s : s_strategies)
        {
            uint64 key = (static_cast<uint64>(s.classId) << 40) | (static_cast<uint64>(s.specId) << 8) | static_cast<uint64>(s.role);
            registeredKeys[key]++;
            if (registeredKeys[key] > 1)
            {
                LOG_ERROR("module.coa-playerbots", "SpecStrategyRegistry: DUPLICATE strategy registration: class={}, spec={}, role={}",
                    s.classId, s.specId, static_cast<uint32>(s.role));
            }
        }

        // Census validation across canonical specs (custom classes 12 to 32)
        uint32 verified = 0;
        uint32 totalCanonical = 0;
        for (uint8 cId = 12; cId <= 32; ++cId)
        {
            auto specs = GetAllSpecs(cId);
            for (auto const& sp : specs)
            {
                totalCanonical++;
                SpecStrategy const* strat = FindStrategy(cId, sp.specId, sp.role);
                if (!strat)
                {
                    LOG_ERROR("module.coa-playerbots", "SpecStrategyRegistry: MISSING strategy for canonical spec: class={}, spec={}, role={}, name='{}'",
                        cId, sp.specId, static_cast<uint32>(sp.role), sp.name);
                }
                else
                {
                    verified++;
                }
            }
        }

        LOG_INFO("module.coa-playerbots", "SpecStrategyRegistry: Census check: verified {} / {} canonical specs.",
            verified, totalCanonical);
    }
}
