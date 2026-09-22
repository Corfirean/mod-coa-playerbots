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
        uint32 requiredAuraOnCaster = 0;
        uint32 missingAuraOnCaster = 0;

        // Internal rotation cooldown / throttle (ms) for abilities without native DBC cooldowns
        uint32 internalThrottleMs = 0;

        // AoE eligibility (item 7 of the Phase 2 fixup pass): a flat `nearbyEnemyCount * bonus`
        // wasn't enough to stop an AoE ability outscoring a single-target one at 1-2 targets just
        // because its baseScore happened to be close. minAoETargets is a hard eligibility floor
        // (ScoreAbility disqualifies below it); the default of 3 matches AbilityTag::AoEDamage's
        // own "Area of Effect offensive" intent, but a profile can lower it (e.g. to 2) for a
        // specific spell that's genuinely worth it that early, or a spell that's formally AoE-
        // shaped but authored/used as a single-target filler can leave AoEDamage off entirely.
        uint8 minAoETargets = 3;

        // Resource management (item 13 of the combat-engine rework). All three default to
        // "unrestricted" (0/0/1) so existing profiles that don't set them are unaffected --
        // per-ability tuning is Phase 3 work, this is just the engine plumbing for it.
        //   minPowerPct: hard eligibility floor -- CanCast disqualifies below this (e.g. a
        //     builder/spender finisher that's pointless below its real resource cost).
        //   reservePowerPct: soft floor -- ScoreAbility deprioritizes (not disqualifies) this
        //     ability once the bot's power is below it, so a healer facing empty mana favors an
        //     efficient heal over a big expensive one, and a tank holds mana/rage for defensives
        //     instead of an optional filler.
        //   resourceEfficiency: relative "value per point of resource spent" (1.0 = neutral).
        //     Above 1.0 nudges this ability up once power is scarce (an efficient option);
        //     below 1.0 nudges it down (a wasteful one) -- see ScoreAbility's resource-aware bonus.
        float minPowerPct = 0.0f;
        float reservePowerPct = 0.0f;
        float resourceEfficiency = 1.0f;

        // Optional custom score function: return added score, or < 0 to disqualify
        std::function<float(CombatContext const&, AbilityDescriptor const&)> customScorer = nullptr;
    };
}

#endif // COA_PLAYERBOTS_ABILITY_DESCRIPTOR_H
