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
#include <functional>
#include <unordered_map>
#include <vector>

class Player;

namespace BotAI
{
    struct CombatContext;

    // -----------------------------------------------------------------------
    // Combat phase -- drives per-phase score modifiers on ability tags.
    // The SpecStrategy's detectPhase() callback returns one of these each tick
    // so the ActionEvaluator can weight abilities appropriately for the current
    // fight state rather than relying on a single fixed priority order.
    // -----------------------------------------------------------------------
    enum class CombatPhase : uint8
    {
        Idle,           // Not in combat, between pulls
        PrePull,        // In combat but opener window hasn't started yet
        Opener,         // First 3-5 seconds, establish rotation / threat
        SingleTarget,   // Normal single-target rotation
        Cleave,         // 2 engaged enemies
        AoE,            // 3+ engaged enemies
        Execute,        // Primary target below execute threshold (typically <20% HP)
        Burst,          // Cooldown window active or target is high-value
        Emergency,      // Self or ally in critical danger
        Recovery,       // OOM / low resource, conserve
    };

    // -----------------------------------------------------------------------
    // RequiredCombatState -- mandatory form/stance/aspect that must be active
    // before the spec's combat rotation can meaningfully execute.
    // -----------------------------------------------------------------------
    struct RequiredCombatState
    {
        // The spell ID the bot must cast to enter its baseline form (0 = none).
        uint32 formSpellId = 0;

        // The aura ID to check for "is the bot currently in this form?"
        // Often the same as formSpellId, but some forms apply a different
        // aura than the cast spell.
        uint32 formAuraId = 0;

        // Alternate forms the spec may temporarily enter (e.g. a tank form
        // that briefly shifts to DPS form for a burst phase, then returns).
        // Not currently used by the engine -- reserved for future form-dancing.
        std::vector<uint32> alternateFormAuras;
    };

    // -----------------------------------------------------------------------
    // Phase score modifier -- applied by ActionEvaluator on top of the
    // ability's own baseScore when the current CombatPhase matches.
    // Multiplier of 1.0 = no change, >1.0 = prioritize, <1.0 = deprioritize,
    // 0.0 = effectively disable (score → 0). Additive bonus is applied after
    // the multiplier.
    // -----------------------------------------------------------------------
    struct PhaseScoreModifier
    {
        AbilityTag tag = AbilityTag::None;
        float multiplier = 1.0f;
        float additive = 0.0f;
    };

    // -----------------------------------------------------------------------
    // SpecStrategy -- the per-spec strategic policy descriptor.
    // One of these is registered per (classId, specId, role) triple alongside
    // the corresponding CombatProfile. The engine consults it every tick for
    // form gating, phase detection, and score modifiers.
    // -----------------------------------------------------------------------
    struct SpecStrategy
    {
        uint8 classId = 0;
        uint32 specId = 0;
        BotRole role = BotRole::Dps;
        char const* strategyName = "";

        // Mandatory form/stance
        RequiredCombatState requiredState;

        // Pre-pull readiness check -- returns true when the bot is ready to
        // engage (form active, buffs up, sufficient resources). nullptr = always ready.
        std::function<bool(Player* bot, CombatContext const&)> isReadyToPull = nullptr;

        // Phase detection -- called each combat tick, returns the current phase.
        // nullptr = always SingleTarget.
        std::function<CombatPhase(CombatContext const&)> detectPhase = nullptr;

        // Per-phase score modifiers. Keyed by CombatPhase, each entry is a list
        // of tag-based multipliers/additives applied during that phase.
        std::unordered_map<CombatPhase, std::vector<PhaseScoreModifier>> phaseModifiers;

        // Resource management policy
        float minResourceToEngage = 0.0f;    // Don't pull if below this % (healer mana, etc.)
        float recoveryThreshold = 15.0f;     // Enter Recovery phase below this %

        // Pet/minion readiness -- return true if all expected pets/minions are
        // present, false if the bot needs to resummon. nullptr = no pet spec.
        std::function<bool(Player* bot)> isPetReady = nullptr;
    };
}

#endif // COA_PLAYERBOTS_SPEC_STRATEGY_H
