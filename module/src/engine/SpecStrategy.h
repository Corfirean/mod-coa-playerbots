/*
 * mod-coa-playerbots
 *
 * Data-Driven Combat AI Framework: SpecStrategy
 * Per-specialization strategic policy layer sitting alongside CombatProfile.
 * CombatProfile = spell metadata / candidates (what the bot CAN cast)
 * SpecStrategy  = state + resource + phase + tactical policy (HOW and WHEN)
 */

#ifndef COA_PLAYERBOTS_SPEC_STRATEGY_H
#define COA_PLAYERBOTS_SPEC_STRATEGY_H

#include "Define.h"
#include "engine/AbilityDescriptor.h"
#include "engine/BotAction.h"
#include "engine/CombatResource.h"
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

class Player;

namespace BotAI
{
    struct CombatContext;

    // -----------------------------------------------------------------------
    // Combat phase -- drives per-phase score modifiers on ability tags.
    // -----------------------------------------------------------------------
    enum class CombatPhase : uint8
    {
        Idle,           // Not in combat, between pulls
        PrePull,        // Preparing pack, pre-buffing, positioning
        Opener,         // First 3-5 seconds or first N casts, establish rotation / threat
        SingleTarget,   // Normal single-target rotation
        Cleave,         // 2 engaged enemies
        AoE,            // 3+ engaged enemies
        Execute,        // Primary target below execute threshold (typically <20% HP)
        Burst,          // Cooldown window active or target is high-value
        Emergency,      // Self or ally in critical danger
        Recovery,       // OOM / low resource, conserve and prioritize builders
    };

    // -----------------------------------------------------------------------
    // Pull readiness result -- reason why a group or bot is ready or not.
    // -----------------------------------------------------------------------
    enum class PullReadinessResult : uint8
    {
        Ready = 0,
        TankNotReady,
        HealerNotReady,
        WaitingForResource,
        WaitingForForm,
        WaitingForBuff,
        WaitingForPet,
        Recovering,
        GroupMemberResting,
        GroupMemberDead,
        GroupMemberFar
    };

    struct PullReadinessInfo
    {
        PullReadinessResult result = PullReadinessResult::Ready;
        char const* reason = "Ready";
        bool tankReady = false;
        bool healerReady = false;

        bool IsReady() const { return result == PullReadinessResult::Ready; }
    };

    // -----------------------------------------------------------------------
    // Combat state status -- evaluates whether the bot is in its required form/stance.
    // -----------------------------------------------------------------------
    enum class CombatStateStatus : uint8
    {
        Ready = 0,               // In baseline form/stance
        NeedEnterBaseline,       // Missing baseline, can cast baseline form spell immediately
        TemporaryAlternate,      // In an allowed temporary form (e.g. movement, emergency defensive)
        ReturningToBaseline,     // Temporary form expired/condition ended, must switch back to baseline
        CannotEnter,             // Cannot enter form (e.g. OOM, stunned, silence)
        SetupRequired            // Missing prerequisite resource before form can be entered (e.g. Scrap for Mechsuit)
    };

    // -----------------------------------------------------------------------
    // StateTransitionRule -- defines conditions to enter and exit a temporary form.
    // -----------------------------------------------------------------------
    struct StateTransitionRule
    {
        uint32 targetSpellId = 0;
        uint32 targetAuraId = 0;
        uint32 minHoldTimeMs = 3000; // Hysteresis: minimum hold time to prevent flapping
        std::function<bool(Player* bot, CombatContext const& ctx)> shouldEnter = nullptr;
        std::function<bool(Player* bot, CombatContext const& ctx)> shouldExit = nullptr;
    };

    // -----------------------------------------------------------------------
    // RequiredCombatState -- mandatory form/stance/aspect management.
    // -----------------------------------------------------------------------
    struct RequiredCombatState
    {
        uint32 formSpellId = 0;
        uint32 formAuraId = 0;

        // Alternate forms supported for backwards compatibility
        std::vector<uint32> alternateFormAuras;

        // Structured form-dancing transition rules
        std::vector<StateTransitionRule> transitionRules;

        // Setup spell to acquire prerequisites if baseline form cannot be cast yet (e.g. Scrap builder)
        uint32 setupSpellId = 0;
        std::function<bool(Player* bot, CombatContext const& ctx)> isSetupNeeded = nullptr;
        std::function<BotAction(Player* bot, CombatContext const& ctx)> buildStateRecoveryAction = nullptr;
    };

    // -----------------------------------------------------------------------
    // Phase score modifier
    // -----------------------------------------------------------------------
    struct PhaseScoreModifier
    {
        AbilityTag tag = AbilityTag::None;
        float multiplier = 1.0f;
        float additive = 0.0f;
    };

    // -----------------------------------------------------------------------
    // Resource policy for multi-resource channels (defensive reserve, min pull)
    // -----------------------------------------------------------------------
    struct ResourcePolicy
    {
        CombatResourceKey key;
        int32 minToEngage = 0;
        int32 defensiveReserve = 0;
        int32 overcapThreshold = 0;
        bool reserveForDefensive = false;
        bool allowDumpDuringBurst = true;
    };

    // -----------------------------------------------------------------------
    // SpecStrategyRuntime -- per-bot combat state tracked in memory.
    // -----------------------------------------------------------------------
    struct SpecStrategyRuntime
    {
        CombatPhase phase = CombatPhase::SingleTarget;
        uint32 combatStartMs = 0;
        uint32 lastFormTransitionMs = 0;
        uint32 lastBaselineStateMs = 0;
        uint32 successfulCastsCount = 0;
        bool openerCompleted = false;
        CombatStateStatus lastStateStatus = CombatStateStatus::Ready;
        std::string lastPullFailureReason = "";
    };

    // -----------------------------------------------------------------------
    // SpecStrategy -- the per-spec strategic policy descriptor.
    // -----------------------------------------------------------------------
    struct SpecStrategy
    {
        uint8 classId = 0;
        uint32 specId = 0;
        BotRole role = BotRole::Dps;
        char const* strategyName = "";

        // Mandatory form/stance
        RequiredCombatState requiredState;

        // Pre-pull readiness check -- returns true when the bot is ready to engage
        std::function<bool(Player* bot, CombatContext const&)> isReadyToPull = nullptr;

        // Phase detection callback
        std::function<CombatPhase(CombatContext const&)> detectPhase = nullptr;

        // Per-phase score modifiers
        std::unordered_map<CombatPhase, std::vector<PhaseScoreModifier>> phaseModifiers;

        // Resource management policy
        float minResourceToEngage = 0.0f;    // Don't pull if primary power below this %
        float recoveryThreshold = 15.0f;     // Enter Recovery phase below this %
        std::vector<ResourcePolicy> resourcePolicies;

        // Pet/minion readiness
        std::function<bool(Player* bot)> isPetReady = nullptr;

        // Check if legacy fallback is permitted given current combat state
        bool CanUseLegacyFallback(CombatContext const& ctx) const;
    };
}

#endif // COA_PLAYERBOTS_SPEC_STRATEGY_H
