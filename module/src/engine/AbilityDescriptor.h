/*
 * mod-coa-playerbots
 *
 * Data-Driven Combat AI Framework: AbilityDescriptor & Tags
 */

#ifndef COA_PLAYERBOTS_ABILITY_DESCRIPTOR_H
#define COA_PLAYERBOTS_ABILITY_DESCRIPTOR_H

#include "Define.h"
#include <functional>

namespace BotAI
{
    struct CombatContext;
    struct AbilityDescriptor;

    enum class AbilityTag : uint32
    {
        None             = 0,
        EmergencyHeal    = 1 << 0,  // Critical priority direct heal (< 40% HP)
        DirectHeal       = 1 << 1,  // Standard direct heal
        PeriodicHeal     = 1 << 2,  // Heal over Time (HoT)
        AoEHeal          = 1 << 3,  // Group / multi-target heal
        Cleanse          = 1 << 4,  // Dispel / remove debuff
        Shield           = 1 << 5,  // Damage absorption / bubble
        DefensiveCD      = 1 << 6,  // Major personal / external defensive cooldown
        Taunt            = 1 << 7,  // Aggro generation / taunt
        MeleeAttack      = 1 << 8,  // Close-range physical / weapon strike
        RangedAttack     = 1 << 9,  // Ranged projectile / spell nuke
        PeriodicDamage   = 1 << 10, // Damage over Time (DoT)
        AoEDamage        = 1 << 11, // Area of Effect offensive
        Execute          = 1 << 12, // Sub-20% HP finisher
        Interrupt        = 1 << 13, // Cast interruption / silence
        OffensiveCD      = 1 << 14, // Major burst cooldown
        CrowdControl     = 1 << 15, // Stun / polymorph / fear
        Buff             = 1 << 16, // Party / self long-term buff
        TotemOrWard      = 1 << 17, // Placed object / totem / ward
        Filler           = 1 << 18  // Low-priority resource builder / spammer
    };

    constexpr AbilityTag operator|(AbilityTag a, AbilityTag b)
    {
        return static_cast<AbilityTag>(static_cast<uint32>(a) | static_cast<uint32>(b));
    }

    constexpr AbilityTag operator&(AbilityTag a, AbilityTag b)
    {
        return static_cast<AbilityTag>(static_cast<uint32>(a) & static_cast<uint32>(b));
    }

    constexpr bool HasTag(AbilityTag mask, AbilityTag tag)
    {
        return (static_cast<uint32>(mask) & static_cast<uint32>(tag)) != 0;
    }

    enum class TargetType : uint8
    {
        Self,
        CurrentTarget,
        LowestHealthAlly,
        TankAlly,
        AnyInjuredAlly,
        PartyMissingBuff,
        AreaHostile
    };

    enum class StateRequirement : uint8
    {
        Any = 0,             // Can be cast in any valid state
        BaselineOnly,        // Can ONLY be cast in baseline combat state (e.g. normal rotation)
        AllowedInTemporary,  // Permitted in temporary alternate state (e.g. movement, burst form)
        EmergencyOnly,       // Permitted outside baseline form only during extreme emergency
        StateTransition      // Form shift / stance change ability itself
    };

    enum class TrackedEntityType : uint8
    {
        None = 0,
        Pet,                 // Main pet (e.g. Houndmaster hound)
        Minion,              // Summoned minion / undead army (e.g. Necromancer)
        Turret,              // Placed mechanical device (e.g. Tinker ZIGGI / Destructo-Bot)
        Ward                 // Placed ward / totem (e.g. Witch Doctor Serpent / Healing Ward)
    };

    struct AbilityDescriptor
    {
        char const* name = "";
        uint32 rootSpellId = 0;
        AbilityTag tags = AbilityTag::None;
        TargetType targetType = TargetType::CurrentTarget;
        float baseScore = 100.0f;

        // Health thresholds (0.0f - 100.0f)
        float minTargetHpPct = 0.0f;
        float maxTargetHpPct = 100.0f;
        float minSelfHpPct = 0.0f;
        float maxSelfHpPct = 100.0f;

        // Group / multi-target conditions
        uint8 minInjuredAllies = 0;
        float injuredAllyHpPctThreshold = 80.0f;

        // Aura requirements
        bool requireAuraMissingOnTarget = false;
        uint32 targetAuraId = 0;             // Explicit aura to check on target if different from root/resolved spell
        uint32 refreshBelowMs = 0;           // Only refresh DoT/HoT/buff if remaining duration < ms
        uint8 refreshBelowStacks = 0;        // Only refresh stackable aura if stacks < count
        uint32 requiredAuraOnCaster = 0;
        uint32 missingAuraOnCaster = 0;

        // Form / state requirement
        StateRequirement stateRequirement = StateRequirement::Any;

        // Entity tracking (Pet / Minion / Turret / Ward)
        TrackedEntityType trackedEntityType = TrackedEntityType::None;
        uint32 trackedEntityEntry = 0;

        // Internal rotation cooldown / throttle (ms) for abilities without native DBC cooldowns
        uint32 internalThrottleMs = 0;

        // AoE eligibility
        uint8 minAoETargets = 3;

        // Resource management
        float minPowerPct = 0.0f;
        float reservePowerPct = 0.0f;
        float resourceEfficiency = 1.0f;

        // Optional custom score function: return added score, or < 0 to disqualify
        std::function<float(CombatContext const&, AbilityDescriptor const&)> customScorer = nullptr;
    };
}

#endif // COA_PLAYERBOTS_ABILITY_DESCRIPTOR_H
