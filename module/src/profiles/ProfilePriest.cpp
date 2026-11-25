/*
 * mod-coa-playerbots
 *
 * Classic / Reborn Combat Profile: Priest (Class 5)
 */

#include "profiles/ProfilePriest.h"
#include "profiles/ProfileRegistry.h"
#include "profiles/ProfileHelpers.h"
#include "engine/CombatContext.h"
#include "Player.h"

namespace BotAI
{
    void RegisterPriestProfiles()
    {
        // =========================================================================
        // 1. SPEC 1 & 2: DISCIPLINE & HOLY (HEALER)
        // =========================================================================
        for (uint32 specId : { 1u, 2u })
        {
            CombatProfile p;
            p.classId = 5;
            p.specId = specId;
            p.role = BotRole::Healer;
            p.profileName = specId == 1 ? "Priest_Discipline_Healer" : "Priest_Holy_Healer";
            p.preferredEngageDistance = PROFILE_RANGED_ENGAGE_DISTANCE;

            // Power Word: Shield (< 85% HP)
            {
                AbilityDescriptor d;
                d.name = "Power Word: Shield";
                d.rootSpellId = 17;
                d.tags = AbilityTag::Shield | AbilityTag::EmergencyHeal;
                d.targetType = TargetType::LowestHealthAlly;
                d.maxTargetHpPct = 85.0f;
                d.requireAuraMissingOnTarget = true;
                d.baseScore = 420.0f;
                p.abilities.push_back(d);
            }

            // Flash Heal (< 75% HP)
            {
                AbilityDescriptor d;
                d.name = "Flash Heal";
                d.rootSpellId = 2061;
                d.tags = AbilityTag::EmergencyHeal | AbilityTag::DirectHeal;
                d.targetType = TargetType::LowestHealthAlly;
                d.maxTargetHpPct = 75.0f;
                d.baseScore = 360.0f;
                p.abilities.push_back(d);
            }

            // Renew (HoT < 90% HP)
            {
                AbilityDescriptor d;
                d.name = "Renew";
                d.rootSpellId = 139;
                d.tags = AbilityTag::PeriodicHeal;
                d.targetType = TargetType::LowestHealthAlly;
                d.maxTargetHpPct = 90.0f;
                d.requireAuraMissingOnTarget = true;
                d.baseScore = 300.0f;
                p.abilities.push_back(d);
            }

            // Greater Heal (< 50% HP)
            {
                AbilityDescriptor d;
                d.name = "Greater Heal";
                d.rootSpellId = 2060;
                d.tags = AbilityTag::DirectHeal;
                d.targetType = TargetType::LowestHealthAlly;
                d.maxTargetHpPct = 50.0f;
                d.baseScore = 380.0f;
                p.abilities.push_back(d);
            }

            // Prayer of Healing (Group Heal)
            {
                AbilityDescriptor d;
                d.name = "Prayer of Healing";
                d.rootSpellId = 596;
                d.tags = AbilityTag::AoEHeal;
                d.targetType = TargetType::LowestHealthAlly;
                d.minInjuredAllies = 3;
                d.injuredAllyHpPctThreshold = 75.0f;
                d.baseScore = 350.0f;
                p.abilities.push_back(d);
            }

            // Penance
            {
                AbilityDescriptor d;
                d.name = "Penance";
                d.rootSpellId = 47540;
                d.tags = AbilityTag::DirectHeal;
                d.targetType = TargetType::LowestHealthAlly;
                d.maxTargetHpPct = 70.0f;
                d.internalThrottleMs = 12000;
                d.baseScore = 400.0f;
                p.abilities.push_back(d);
            }

            // Circle of Healing
            {
                AbilityDescriptor d;
                d.name = "Circle of Healing";
                d.rootSpellId = 34861;
                d.tags = AbilityTag::AoEHeal;
                d.targetType = TargetType::LowestHealthAlly;
                d.minInjuredAllies = 3;
                d.baseScore = 390.0f;
                p.abilities.push_back(d);
            }

            // Prayer of Mending
            {
                AbilityDescriptor d;
                d.name = "Prayer of Mending";
                d.rootSpellId = 33076;
                d.tags = AbilityTag::PeriodicHeal;
                d.targetType = TargetType::TankAlly;
                d.baseScore = 345.0f;
                d.internalThrottleMs = 10000;
                p.abilities.push_back(d);
            }

            // Pain Suppression
            {
                AbilityDescriptor d;
                d.name = "Pain Suppression";
                d.rootSpellId = 33206;
                d.tags = AbilityTag::EmergencyHeal;
                d.targetType = TargetType::LowestHealthAlly;
                d.baseScore = 475.0f;
                d.maxTargetHpPct = 35.0f;
                d.internalThrottleMs = 60000;
                p.abilities.push_back(d);
            }

            // Guardian Spirit
            {
                AbilityDescriptor d;
                d.name = "Guardian Spirit";
                d.rootSpellId = 47788;
                d.tags = AbilityTag::EmergencyHeal;
                d.targetType = TargetType::LowestHealthAlly;
                d.baseScore = 470.0f;
                d.maxTargetHpPct = 30.0f;
                d.internalThrottleMs = 60000;
                p.abilities.push_back(d);
            }

            // Inner Fire
            {
                AbilityDescriptor d;
                d.name = "Inner Fire";
                d.rootSpellId = 588;
                d.tags = AbilityTag::Buff;
                d.targetType = TargetType::Self;
                d.baseScore = 340.0f;
                d.missingAuraOnCaster = 588;
                p.abilities.push_back(d);
            }

            // Psychic Scream (survival: a hurt priest fears what is hitting it)
            {
                AbilityDescriptor d;
                d.name = "Psychic Scream";
                d.rootSpellId = 8122;
                d.tags = AbilityTag::CrowdControl;
                d.targetType = TargetType::Self;
                d.baseScore = 335.0f;
                d.maxSelfHpPct = 35.0f;
                d.customScorer = [](CombatContext const& ctx, AbilityDescriptor const&) -> float {
                    return (!ctx.targetIsBossOrElite && ctx.engagedEnemyCount >= 1) ? 0.0f : -1.0f;
                };
                p.abilities.push_back(d);
            }

            ProfileRegistry::RegisterProfile(p);
        }

        // =========================================================================
        // 2. SPEC 3: SHADOW (DPS)
        // =========================================================================
        {
            CombatProfile p;
            p.classId = 5;
            p.specId = 3;
            p.role = BotRole::Dps;
            p.profileName = "Priest_Shadow_Dps";
            p.preferredEngageDistance = PROFILE_RANGED_ENGAGE_DISTANCE;

            // Shadowform
            {
                AbilityDescriptor d;
                d.name = "Shadowform";
                d.rootSpellId = 15473;
                d.tags = AbilityTag::Buff;
                d.targetType = TargetType::Self;
                d.missingAuraOnCaster = 15473;
                d.baseScore = 500.0f;
                p.abilities.push_back(d);
            }

            // Vampiric Touch
            {
                AbilityDescriptor d;
                d.name = "Vampiric Touch";
                d.rootSpellId = 34914;
                d.tags = AbilityTag::PeriodicDamage;
                d.targetType = TargetType::CurrentTarget;
                d.requireAuraMissingOnTarget = true;
                d.baseScore = 340.0f;
                p.abilities.push_back(d);
            }

            // Shadow Word: Pain
            {
                AbilityDescriptor d;
                d.name = "Shadow Word: Pain";
                d.rootSpellId = 589;
                d.tags = AbilityTag::PeriodicDamage;
                d.targetType = TargetType::CurrentTarget;
                d.requireAuraMissingOnTarget = true;
                d.baseScore = 320.0f;
                p.abilities.push_back(d);
            }

            // Mind Blast
            {
                AbilityDescriptor d;
                d.name = "Mind Blast";
                d.rootSpellId = 8092;
                d.tags = AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 300.0f;
                p.abilities.push_back(d);
            }

            // Mind Flay (Channeled Filler)
            {
                AbilityDescriptor d;
                d.name = "Mind Flay";
                d.rootSpellId = 15407;
                d.tags = AbilityTag::Filler | AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 200.0f;
                p.abilities.push_back(d);
            }

            // Smite (Fallback Filler)
            {
                AbilityDescriptor d;
                d.name = "Smite";
                d.rootSpellId = 585;
                d.tags = AbilityTag::Filler | AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 150.0f;
                p.abilities.push_back(d);
            }

            // Devouring Plague
            {
                AbilityDescriptor d;
                d.name = "Devouring Plague";
                d.rootSpellId = 2944;
                d.tags = AbilityTag::PeriodicDamage;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 330.0f;
                d.requireAuraMissingOnTarget = true;
                p.abilities.push_back(d);
            }

            // Shadow Word: Death
            {
                AbilityDescriptor d;
                d.name = "Shadow Word: Death";
                d.rootSpellId = 32379;
                d.tags = AbilityTag::Execute | AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 390.0f;
                d.maxTargetHpPct = 25.0f;
                d.customScorer = [](CombatContext const& ctx, AbilityDescriptor const&) -> float {
                    return ctx.botHpPct > 40.0f ? 0.0f : -1.0f;  // it hurts the caster
                };
                p.abilities.push_back(d);
            }

            // Silence
            {
                AbilityDescriptor d;
                d.name = "Silence";
                d.rootSpellId = 15487;
                d.tags = AbilityTag::Interrupt | AbilityTag::RangedAttack;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 395.0f;
                p.abilities.push_back(d);
            }

            // Shadowfiend
            {
                AbilityDescriptor d;
                d.name = "Shadowfiend";
                d.rootSpellId = 34433;
                d.tags = AbilityTag::OffensiveCD;
                d.targetType = TargetType::CurrentTarget;
                d.baseScore = 330.0f;
                d.customScorer = [](CombatContext const& ctx, AbilityDescriptor const&) -> float {
                    return ctx.botPowerPct < 70.0f ? 0.0f : -1.0f;
                };
                p.abilities.push_back(d);
            }

            // Dispersion
            {
                AbilityDescriptor d;
                d.name = "Dispersion";
                d.rootSpellId = 47585;
                d.tags = AbilityTag::Buff;
                d.targetType = TargetType::Self;
                d.baseScore = 360.0f;
                d.missingAuraOnCaster = 47585;
                d.customScorer = [](CombatContext const& ctx, AbilityDescriptor const&) -> float {
                    return ctx.botPowerPct < 30.0f ? 0.0f : -1.0f;
                };
                p.abilities.push_back(d);
            }

            // Inner Fire
            {
                AbilityDescriptor d;
                d.name = "Inner Fire";
                d.rootSpellId = 588;
                d.tags = AbilityTag::Buff;
                d.targetType = TargetType::Self;
                d.baseScore = 340.0f;
                d.missingAuraOnCaster = 588;
                p.abilities.push_back(d);
            }

            // Psychic Scream (survival: a hurt priest fears what is hitting it)
            {
                AbilityDescriptor d;
                d.name = "Psychic Scream";
                d.rootSpellId = 8122;
                d.tags = AbilityTag::CrowdControl;
                d.targetType = TargetType::Self;
                d.baseScore = 335.0f;
                d.maxSelfHpPct = 35.0f;
                d.customScorer = [](CombatContext const& ctx, AbilityDescriptor const&) -> float {
                    return (!ctx.targetIsBossOrElite && ctx.engagedEnemyCount >= 1) ? 0.0f : -1.0f;
                };
                p.abilities.push_back(d);
            }

            ProfileRegistry::RegisterProfile(p);
        }
    }
}
